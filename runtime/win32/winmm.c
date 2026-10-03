/* winmm.c -- multimedia timers. timeSetEvent runs each timer on its own host thread with its own
 * guest stack and TIB, calling the guest TimeProc (or setting/pulsing an event) at the period. */
#include "win32.h"
#include <stdlib.h>
#include <time.h>

HLE(timeGetTime) { (void)a; return rt_ms(); }
HLE(timeGetDevCaps) { wr32(a[0], 1); wr32(a[0] + 4, 1000000); return 0; }   /* TIMECAPS {wPeriodMin, wPeriodMax} */
HLE(timeBeginPeriod) { (void)a; return 0; }
HLE(timeEndPeriod) { (void)a; return 0; }
HLE(waveOutGetNumDevs) { (void)a; return 1; }

#define MAX_TIMERS 64
#define TIMER_STACK (256u << 10)
typedef struct { uint32_t id, delay, proc, user, flags, stack, tib; volatile int killed; } mmtimer_t;
static mmtimer_t *g_timers[MAX_TIMERS];
static pthread_mutex_t g_tmu = PTHREAD_MUTEX_INITIALIZER;

static void sleep_until(const struct timespec *t) {   /* no clock_nanosleep on macOS */
    for (;;) {
        struct timespec now, d;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long long ns = (long long)(t->tv_sec - now.tv_sec) * 1000000000LL + (t->tv_nsec - now.tv_nsec);
        if (ns <= 0) return;
        d.tv_sec = (time_t)(ns / 1000000000LL); d.tv_nsec = (long)(ns % 1000000000LL);
        nanosleep(&d, NULL);
    }
}

static void *timer_main(void *arg) {
    mmtimer_t *t = arg;
    rt_thread_enter(t->stack, t->stack + TIMER_STACK, t->tib);
    struct timespec next;
    clock_gettime(CLOCK_MONOTONIC, &next);
    do {
        next.tv_nsec += (long)t->delay * 1000000L;
        next.tv_sec += next.tv_nsec / 1000000000L;
        next.tv_nsec %= 1000000000L;
        sleep_until(&next);
        if (t->killed) break;
        if (t->flags & 0x30) {                     /* TIME_CALLBACK_EVENT_SET / _PULSE: proc is an event */
            hobj_t *e = h_get(t->proc, H_EVENT);
            if (e) h_signal(e);
        } else {
            guest_call(t->proc, 5, t->id, 0, t->user, 0, 0);   /* TimeProc(id, msg, user, dw1, dw2) */
        }
    } while ((t->flags & 1) && !t->killed);       /* TIME_PERIODIC */
    return NULL;
}

HLE(timeSetEvent) {   /* (delay ms, resolution, proc, user, flags) -> timer id, 0 on failure */
    if (!a[0] || !a[2]) return 0;
    pthread_mutex_lock(&g_tmu);
    int i = 0;
    while (i < MAX_TIMERS && g_timers[i]) i++;
    if (i == MAX_TIMERS) { pthread_mutex_unlock(&g_tmu); return 0; }
    mmtimer_t *t = calloc(1, sizeof *t);
    *t = (mmtimer_t){ .id = (uint32_t)i + 1, .delay = a[0], .proc = a[2], .user = a[3], .flags = a[4],
                      .stack = gmemalign(16, TIMER_STACK), .tib = gmemalign(0x1000, 0x1000) };
    g_timers[i] = t;
    pthread_mutex_unlock(&g_tmu);
    pthread_t pt;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 8u << 20);
    if (pthread_create(&pt, &at, timer_main, t)) { g_timers[i] = NULL; free(t); return 0; }
    pthread_detach(pt);
    rt_log("timeSetEvent(%u ms, proc 0x%08X, user 0x%08X, flags 0x%X) -> %u", a[0], a[2], a[3], a[4], t->id);
    return t->id;
}

/* The thread notices on its next tick and exits; its memory is not reused (a callback may be running). */
HLE(timeKillEvent) {
    uint32_t i = a[0] - 1;
    if (a[0] == 0 || i >= MAX_TIMERS) return 97;   /* MMSYSERR_INVALPARAM */
    pthread_mutex_lock(&g_tmu);
    mmtimer_t *t = g_timers[i];
    if (t) { t->killed = 1; g_timers[i] = NULL; }
    pthread_mutex_unlock(&g_tmu);
    return t ? 0 : 97;
}
