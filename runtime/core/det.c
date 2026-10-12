/* det.c -- deterministic mode (RT_DET=1; every netplay session runs in it).
 * Same inputs in -> same guest state out, on any host and any timing:
 *  - Virtual time: every guest-visible clock (RDTSC, QPC, GetTickCount, timeGetTime, file time,
 *    timers, Sleep and wait timeouts) reads one clock. Present advances it to the next 1/60 s slot;
 *    when every guest thread is waiting it jumps to the earliest deadline. It never reads the host.
 *  - Deterministic scheduling: exactly one guest thread runs (the baton holder). It runs until it
 *    blocks, sleeps, yields (Sleep(0), every 4096 function entries) or presents; the next runnable
 *    thread is picked round-robin in creation order. Readiness depends only on guest state and
 *    virtual time, never on host wake-up order. Replaces the FIFO guest lock (cpu.c) in this mode.
 *  - Host -> guest input crosses only at Present (det_frame): replay/netplay input, then window
 *    messages, in a fixed order.
 *  - det_hash (det_hash.c): 64-bit hash of guest state (image, main stack, heap) for synctests and desync checks. */
#include "rt.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

int rt_det = -1;

#define DET_MAX 64
enum { T_FREE, T_RUN, T_WAIT, T_DEAD };
typedef struct {
    int state;
    det_ready_fn ready;
    void *arg;
    uint64_t deadline;
    pthread_cond_t cv;
} dthr_t;
static dthr_t g_t[DET_MAX];
static int g_n, g_cur;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_vt;                 /* virtual ns since boot */
static uint32_t g_slices;             /* preemption slices since the last Present or idle jump */
static RECOMP_TLS int t_id = -1;
static uint64_t (*g_deadline_fn)(void);   /* winmm: the earliest timer due */
void det_set_deadline_fn(uint64_t (*fn)(void)) { g_deadline_fn = fn; }

int det_on(void) {
    if (rt_det < 0) {
        const char *e = getenv("RT_DET");
        rt_det = e && *e == '1';
    }
    return rt_det;
}
uint64_t det_now_ns(void) { return __atomic_load_n(&g_vt, __ATOMIC_RELAXED); }

/* A guest clock read (RDTSC, QPC, GetTickCount, a sound play cursor...). A thread that reads the
 * clock DET_SPIN times in a row with fewer than DET_SPIN_GAP guest function calls between reads is
 * polling: waiting for time to pass (busy-wait loops: CPU calibration, pacing). It yields and time
 * advances DET_SPIN_NS. Reads scattered through real work (profiling stopwatches) never count, so
 * an ordinary frame's dt stays exactly 1/60 s. Counts are per thread and depend only on the guest's
 * own code path: deterministic. */
#define DET_SPIN     32
#define DET_SPIN_GAP 64
#define DET_SPIN_NS  500000ull
static RECOMP_TLS uint32_t t_reads, t_last_entries;
uint32_t rt_enter_mark(void);   /* log.c: guest function entries on this thread */
uint64_t det_clock_read(void) {
    if (t_id < 0) return det_now_ns();
    uint32_t e = rt_enter_mark();
    if (e - t_last_entries >= DET_SPIN_GAP) t_reads = 0;
    t_last_entries = e;
    if (++t_reads >= DET_SPIN) {
        t_reads = 0;
        __atomic_add_fetch(&g_vt, DET_SPIN_NS, __ATOMIC_RELAXED);
        det_yield();
    }
    return det_now_ns();
}
int det_thread_id(void) { return t_id; }

void det_init_main(void) {
    {
        extern int rt_ff;
        if (getenv("RT_FF")) rt_ff = 1;
    } /* tests: run as if catching up (no drawing, no sound) */ /* the boot thread: slot 0, holds the baton */
    pthread_mutex_lock(&g_mu);
    pthread_cond_init(&g_t[0].cv, NULL);
    g_t[0].state = T_RUN;
    g_n = 1;
    g_cur = 0;
    t_id = 0;
    pthread_mutex_unlock(&g_mu);
    rt_log("det: deterministic mode on");
}

static int runnable(dthr_t *t) {
    if (t->state == T_RUN) return 1;
    if (t->state != T_WAIT) return 0;
    if ((t->ready && t->ready(t->arg)) || t->deadline <= g_vt) {
        t->state = T_RUN;
        return 1;
    }
    return 0;
}

static uint32_t g_scheds, g_handoffs; /* stats: scheduling points, and those that changed thread */

/* g_mu held. Pick the next thread after `me` (me itself last), advancing virtual time while
 * every thread waits; hand it the baton and wait until it comes back (unless me is dead). */
static void schedule(int me) {
    int next = -1;
    t_reads = 0;
    for (;;) {
        for (int i = 1; i <= g_n && next < 0; i++) {
            int j = (me + i) % g_n;
            if (runnable(&g_t[j])) next = j;
        }
        if (next >= 0) break;
        uint64_t m = UINT64_MAX;
        for (int j = 0; j < g_n; j++)
            if (g_t[j].state == T_WAIT && g_t[j].deadline < m) m = g_t[j].deadline;
        if (g_deadline_fn) {
            uint64_t d = g_deadline_fn();
            if (d < m) m = d;
        }
        if (m == UINT64_MAX) rt_fatal("det: deadlock (every guest thread waits with no deadline)");
        __atomic_store_n(&g_vt, m, __ATOMIC_RELAXED);
        g_slices = 0;
    }
    g_scheds++;
    if (next == me) return;
    g_handoffs++;
    static int chaos = -1; /* RT_DET_CHAOS=1: random host delays at every hand-over (synctest) */
    if (chaos < 0) {
        const char *e = getenv("RT_DET_CHAOS");
        chaos = e && *e == '1';
    }
    if (chaos) {
        pthread_mutex_unlock(&g_mu);
        usleep((useconds_t)(rand() % 40));
        pthread_mutex_lock(&g_mu);
    }
    g_cur = next;
    pthread_cond_signal(&g_t[next].cv);
    if (g_t[me].state == T_DEAD) return;
    while (g_cur != me) pthread_cond_wait(&g_t[me].cv, &g_mu);
}

/* Preemption (every 4096 guest function entries, log.c). After DET_STALL slices with no Present
 * and no idle time jump (a loading screen, a movie waiting for its audio clock, a thread polling
 * shared state another thread updates on a timer), time also passes with work, like a CPU clock:
 * DET_WORK_NS per slice. Ordinary frames never get there, so their dt stays 1/60 s. */
#define DET_STALL   256
#define DET_WORK_NS 100000ull
void det_preempt(void) {
    if (t_id < 0) return;
    if (++g_slices > DET_STALL) __atomic_add_fetch(&g_vt, DET_WORK_NS, __ATOMIC_RELAXED);
    det_yield();
}

void det_yield(void) {
    if (t_id < 0) return;
    pthread_mutex_lock(&g_mu);
    schedule(t_id);
    pthread_mutex_unlock(&g_mu);
}

/* Block until ready(arg) holds or virtual time reaches deadline (UINT64_MAX: none).
 * Returns 1 if ready, 0 on timeout. ready() must not change guest state (the woken thread
 * acquires afterwards, still holding the baton, so nothing can intervene). */
int det_wait(det_ready_fn ready, void *arg, uint64_t deadline) {
    if (t_id < 0) rt_fatal("det_wait from a host thread");
    pthread_mutex_lock(&g_mu);
    dthr_t *t = &g_t[t_id];
    if (!(ready && ready(arg)) && deadline > g_vt) {
        t->state = T_WAIT;
        t->ready = ready;
        t->arg = arg;
        t->deadline = deadline;
        schedule(t_id);
    }
    t->state = T_RUN;
    t->ready = NULL;
    int ok = ready && ready(arg);
    pthread_mutex_unlock(&g_mu);
    return ok;
}

/* Sleep(0) gives up the time slice and no time. A thread that calls it DET_SPIN times without a
 * Present in between is waiting for time to pass, as a clock-polling thread is (det_clock_read):
 * the movie player loops `while (!frame_due()) { tasks; SleepEx(0); }` until EA's sound clock
 * reaches the next frame. Its loop body is too long for the clock-read rule, so time crept only
 * by the stall rule (0.1 ms per 4096 function entries): the intro ran at 7 fps in the browser,
 * against 25 natively. From the DET_SPIN-th on, each Sleep(0) passes DET_SPIN_NS. Per-thread
 * counts on the guest's own code path: deterministic. Ordinary frames never get there. */
static volatile uint32_t g_frame_no; /* the last det_frame */
static RECOMP_TLS uint32_t t_s0_frame, t_s0_n;
void det_sleep_ms(uint32_t ms) {
    if (!ms) {
        if (t_s0_frame != g_frame_no) {
            t_s0_frame = g_frame_no;
            t_s0_n = 0;
        }
        if (++t_s0_n >= DET_SPIN) __atomic_add_fetch(&g_vt, DET_SPIN_NS, __ATOMIC_RELAXED);
        det_yield();
        return;
    }
    det_wait(NULL, NULL, det_now_ns() + (uint64_t)ms * 1000000ull);
}

/* Creator side (holds the baton): reserve the next slot, runnable now or once ready(arg). */
int det_thread_new(det_ready_fn ready, void *arg) {
    pthread_mutex_lock(&g_mu);
    if (g_n >= DET_MAX) rt_fatal("det: too many threads");
    int id = g_n++;
    dthr_t *t = &g_t[id];
    pthread_cond_init(&t->cv, NULL);
    t->state = ready ? T_WAIT : T_RUN;
    t->ready = ready;
    t->arg = arg;
    t->deadline = UINT64_MAX;
    pthread_mutex_unlock(&g_mu);
    return id;
}
/* New thread side: wait for the baton. */
void det_thread_begin(int id) {
    pthread_mutex_lock(&g_mu);
    t_id = id;
    while (g_cur != id) pthread_cond_wait(&g_t[id].cv, &g_mu);
    g_t[id].state = T_RUN;
    g_t[id].ready = NULL;
    pthread_mutex_unlock(&g_mu);
}
void det_thread_end(void) {
    if (t_id < 0) return;
    pthread_mutex_lock(&g_mu);
    g_t[t_id].state = T_DEAD;
    schedule(t_id);
    pthread_mutex_unlock(&g_mu);
    t_id = -1;
}

/* ---- frames ---------------------------------------------------------------------------------- */
static det_frame_fn g_hooks[8];
static int g_nhooks;
void det_on_frame(det_frame_fn fn) {
    if (g_nhooks < 8) g_hooks[g_nhooks++] = fn;
}

/* Present: input for this frame, then virtual time moves to the frame's slot and every thread
 * due by then runs before the game continues. */
void det_frame(uint32_t frame) {
    for (int i = 0; i < g_nhooks; i++) g_hooks[i](frame);
    uint64_t slot = (uint64_t)frame * 1000000000ull / 60ull;
    static unsigned over, n; /* frames whose work ran past their 1/60 s slot (dt > 1/60 there) */
    if (slot > g_vt) __atomic_store_n(&g_vt, slot, __ATOMIC_RELAXED);
    else over++;
    g_slices = 0;
    g_frame_no = frame;
    if (++n == 600) {
        rt_log("det: %u of 600 frames ran past their slot; per frame %u scheduling points, %u thread hand-overs", over,
               g_scheds / 600, g_handoffs / 600);
        over = n = 0;
        g_scheds = g_handoffs = 0;
    }
    det_yield();
    det_hash_frame(frame);   /* det_hash.c: RT_DET_HASH / RT_DET_PAGES / RT_DET_DUMP */
}

/* The virtual time the last Present advanced, for the frame pacers (platform/native/frame.c,
   platform/web/platform.c): 1/60 s for an ordinary frame, a whole movie frame (40 ms) when the movie
   player presents only once the next one is due (paced at 1/60 the intro ran 2.4x fast), 0 while
   frames catch up with time a loading jump already passed. Capped: a long stall has already taken
   its wall time. -1 outside deterministic mode. */
double det_period_ms(void) {
    static uint64_t last;
    if (!det_on()) return -1;
    uint64_t vt = det_now_ns();
    double d = last && vt >= last ? (double)(vt - last) / 1e6 : 1000.0 / 60.0;
    last = vt;
    return d > 100.0 ? 100.0 : d;
}
