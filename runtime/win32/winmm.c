/* winmm.c -- time functions and multimedia timers (one timer thread, as on Windows). */
#include "win32.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

HLE(timeGetTime) { (void)a; return rt_ms(); }
HLE(timeGetDevCaps) { wr32(a[0], 1); wr32(a[0] + 4, 1000000); return 0; }   /* TIMECAPS {wPeriodMin, wPeriodMax} */
HLE(timeBeginPeriod) { (void)a; return 0; }
HLE(timeEndPeriod) { (void)a; return 0; }
HLE(waveOutGetNumDevs) { (void)a; return 1; }

/* Multimedia timers. As on Windows, every callback runs on one timer thread (its own guest stack
 * and TIB). Timers sit in a deadline-ordered table; one-shot timers (flags & 1 == 0, which the
 * game re-arms after each tick) are removed once they fire, so ids and slots never run out. */
#define MAX_TIMERS 256
#define TIMER_STACK (256u << 10)
typedef struct { uint32_t id, delay, proc, user, flags; uint64_t due_ns; } mmtimer_t;
static mmtimer_t g_timers[MAX_TIMERS];
static int g_ntimers;
static uint32_t g_next_id;
static pthread_mutex_t g_tmu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_tcv = PTHREAD_COND_INITIALIZER;
static int g_tthread;
/* The timer being delivered on this thread (its ideal due time). The game's clock is a counter its
 * one-shot TimeProc increments once per tick and re-arms from inside the callback (timeSetEvent
 * 8-9 ms): a re-arm scheduled from "now" turns every late wakeup (guest lock, scheduler slop) into
 * lost game time and frame-to-frame dt jitter. Re-arming from the ideal due time makes late ticks
 * catch up, so game time follows the wall clock as on Windows (~1 ms timer latency). */
static __thread uint64_t t_fire_due;
static __thread uint32_t t_fire_proc;

static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static void *timer_main(void *arg) {
    (void)arg;
    uint32_t stack = gmemalign(16, TIMER_STACK), tib = gmemalign(0x1000, 0x1000);
    rt_thread_enter(stack, stack + TIMER_STACK, tib);
    pthread_mutex_lock(&g_tmu);
    for (;;) {
        int next = -1;
        for (int i = 0; i < g_ntimers; i++)
            if (next < 0 || g_timers[i].due_ns < g_timers[next].due_ns) next = i;
        if (next < 0) { pthread_cond_wait(&g_tcv, &g_tmu); continue; }
        uint64_t now = now_ns();
        if (g_timers[next].due_ns > now) {
            uint64_t ns = g_timers[next].due_ns - now;
            struct timespec abs;
            clock_gettime(CLOCK_REALTIME, &abs);           /* pthread_cond_timedwait uses the realtime clock */
            abs.tv_sec += (time_t)(ns / 1000000000ull);
            abs.tv_nsec += (long)(ns % 1000000000ull);
            if (abs.tv_nsec >= 1000000000L) { abs.tv_sec++; abs.tv_nsec -= 1000000000L; }
            pthread_cond_timedwait(&g_tcv, &g_tmu, &abs);
            continue;                                       /* table may have changed */
        }
        mmtimer_t t = g_timers[next];
        t_fire_due = t.due_ns; t_fire_proc = t.proc;
        if (t.flags & 1) g_timers[next].due_ns += (uint64_t)t.delay * 1000000ull;   /* TIME_PERIODIC */
        else g_timers[next] = g_timers[--g_ntimers];                               /* one-shot: done */
        pthread_mutex_unlock(&g_tmu);
        if (t.flags & 0x30) {                     /* TIME_CALLBACK_EVENT_SET / _PULSE: proc is an event */
            hobj_t *e = h_get(t.proc, H_EVENT);
            if (e) h_signal(e);
        } else {
            guest_call(t.proc, 5, t.id, 0, t.user, 0, 0);   /* TimeProc(id, msg, user, dw1, dw2) */
            t_fire_proc = 0;
        }
        pthread_mutex_lock(&g_tmu);
    }
    return NULL;
}

HLE(timeSetEvent) {   /* (delay ms, resolution, proc, user, flags) -> timer id, 0 on failure */
    if (!a[0] || !a[2]) return 0;
    pthread_mutex_lock(&g_tmu);
    if (!g_tthread) {
        pthread_t pt;
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_setstacksize(&at, 8u << 20);
        if (pthread_create(&pt, &at, timer_main, NULL)) { pthread_mutex_unlock(&g_tmu); return 0; }
        pthread_detach(pt);
        g_tthread = 1;
    }
    if (g_ntimers == MAX_TIMERS) { pthread_mutex_unlock(&g_tmu); rt_log("timeSetEvent: %d timers live", MAX_TIMERS); return 0; }
    uint32_t id = ++g_next_id ? g_next_id : ++g_next_id;
    uint64_t now = now_ns(), due = now + (uint64_t)a[0] * 1000000ull;
    if (t_fire_proc && t_fire_proc == a[2]) {   /* re-armed by its own callback: keep the ideal schedule */
        uint64_t ideal = t_fire_due + (uint64_t)a[0] * 1000000ull;
        if (ideal + 250000000ull > now) due = ideal < now ? now : ideal;   /* > 250 ms behind: resync */
    }
    g_timers[g_ntimers++] = (mmtimer_t){ .id = id, .delay = a[0], .proc = a[2], .user = a[3], .flags = a[4],
                                         .due_ns = due };
    pthread_cond_signal(&g_tcv);
    pthread_mutex_unlock(&g_tmu);
    static int logged;
    if (logged++ < 4) rt_log("timeSetEvent(%u ms, proc 0x%08X, user 0x%08X, flags 0x%X) -> %u", a[0], a[2], a[3], a[4], id);
    return id;
}

HLE(timeKillEvent) {
    pthread_mutex_lock(&g_tmu);
    int found = 0;
    for (int i = 0; i < g_ntimers; i++)
        if (g_timers[i].id == a[0]) { g_timers[i] = g_timers[--g_ntimers]; found = 1; break; }
    pthread_cond_signal(&g_tcv);
    pthread_mutex_unlock(&g_tmu);
    return found ? 0 : 97;   /* MMSYSERR_INVALPARAM */
}
