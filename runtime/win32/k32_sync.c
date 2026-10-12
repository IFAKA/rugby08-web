/* k32_sync.c -- critical sections, events, mutexes, semaphores, waits, interlocked ops (TLS, threads
 * and sleeping: k32_thread.c). */
#include "win32.h"
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- critical sections: CRITICAL_SECTION.LockSemaphore (+0x10) holds our mutex index ------ */
#define MAX_CS 8192
static pthread_mutex_t *g_cs[MAX_CS];
static int g_ncs = 1;
static pthread_mutex_t g_csmu = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t *cs_get(uint32_t cs) {
    uint32_t idx = rd32(cs + 0x10);
    if (idx && idx < MAX_CS && g_cs[idx]) return g_cs[idx];
    pthread_mutex_lock(&g_csmu);   /* never initialised: create lazily, like a zeroed static CS */
    idx = rd32(cs + 0x10);
    if (!idx || idx >= MAX_CS || !g_cs[idx]) {
        if (g_ncs >= MAX_CS) rt_fatal("critical section table full");
        pthread_mutexattr_t at;
        pthread_mutexattr_init(&at);
        pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_t *m = malloc(sizeof *m);
        pthread_mutex_init(m, &at);
        idx = (uint32_t)g_ncs++;
        g_cs[idx] = m;
        wr32(cs + 0x10, idx);
    }
    pthread_mutex_unlock(&g_csmu);
    return g_cs[idx];
}

HLE(InitializeCriticalSection) {
    memset(G2H(a[0]), 0, 24);
    wr32(a[0] + 4, 0xFFFFFFFFu);   /* LockCount = -1 */
    cs_get(a[0]);
    return 0;
}
HLE(DeleteCriticalSection) {
    (void)a;
    return 0;
}   /* keep the mutex: cheap, and avoids reuse races */
/* Deterministic mode: only the baton holder runs, so ownership is plain bookkeeping (det.c). */
typedef struct {
    int owner, count;
} dcs_t;
static dcs_t g_dcs[MAX_CS];
static int dcs_free(void *p) { return ((dcs_t *)p)->count == 0; }

HLE(EnterCriticalSection) {
    if (det_on() && det_thread_id() >= 0) {
        cs_get(a[0]);
        dcs_t *d = &g_dcs[rd32(a[0] + 0x10)];
        int me = det_thread_id();
        while (d->count && d->owner != me) det_wait(dcs_free, d, UINT64_MAX);
        d->owner = me;
        d->count++;
        wr32(a[0] + 8, rd32(a[0] + 8) + 1);
        wr32(a[0] + 0xC, rt_thread_tib() >> 12);
        return 0;
    }
    pthread_mutex_lock(cs_get(a[0]));
    wr32(a[0] + 8, rd32(a[0] + 8) + 1);              /* RecursionCount */
    wr32(a[0] + 0xC, rt_thread_tib() >> 12);          /* OwningThread (our thread id) */
    return 0;
}
HLE(LeaveCriticalSection) {
    uint32_t r = rd32(a[0] + 8);
    if (r) wr32(a[0] + 8, r - 1);
    if (r <= 1) wr32(a[0] + 0xC, 0);
    if (det_on() && det_thread_id() >= 0) {
        dcs_t *d = &g_dcs[rd32(a[0] + 0x10)];
        if (d->count) {
            d->count--;
            if (d->count == 0) d->owner = -1;
        }
        return 0;
    }
    pthread_mutex_unlock(cs_get(a[0]));
    return 0;
}

/* ---- events / mutexes / semaphores --------------------------------------------------------- */
/* A Create* call on a name that already exists returns a handle to the existing object and sets
 * ERROR_ALREADY_EXISTS, as Windows does. 0 = no such object: create it. */
static uint32_t existing(htype_t type, uint32_t name_va) {
    if (!name_va) return 0;
    uint32_t h = h_open_named(type, gstr(name_va));
    if (h) rt_last_error = ERROR_ALREADY_EXISTS_;
    return h;
}
static uint32_t named(uint32_t h, uint32_t name_va) {
    if (name_va) snprintf(h_get(h, H_FREE)->name, 260, "%s", gstr(name_va));
    return h;
}
HLE(CreateEventA) {   /* (sa, manual, initial, name) */
    uint32_t ex = existing(H_EVENT, a[3]);
    if (ex) return ex;
    uint32_t h = h_new(H_EVENT);
    hobj_t *o = h_get(h, H_EVENT);
    o->manual_reset = a[1] != 0;
    o->signaled = a[2] != 0;
    return named(h, a[3]);
}
HLE(SetEvent) {
    hobj_t *o = h_get(a[0], H_EVENT);
    if (!o) return set_err(ERROR_INVALID_HANDLE_);
    h_signal(o);
    return TRUE_;
}
HLE(ResetEvent) {
    hobj_t *o = h_get(a[0], H_EVENT);
    if (!o) return set_err(ERROR_INVALID_HANDLE_);
    pthread_mutex_lock(&o->mu);
    o->signaled = 0;
    pthread_mutex_unlock(&o->mu);
    return TRUE_;
}
HLE(CreateMutexA) {   /* (sa, initialOwner, name) */
    uint32_t ex = existing(H_MUTEX, a[2]);
    if (ex) return ex;
    uint32_t h = h_new(H_MUTEX);
    hobj_t *o = h_get(h, H_MUTEX);
    if (a[1]) {
        o->owner = rt_host_tid();
        o->count = 1;
    }
    rt_last_error = 0;
    return named(h, a[2]);
}
HLE(ReleaseMutex) {
    hobj_t *o = h_get(a[0], H_MUTEX);
    if (!o) return set_err(ERROR_INVALID_HANDLE_);
    pthread_mutex_lock(&o->mu);
    if (o->count > 0 && o->owner == rt_host_tid()) {
        o->count--;
        if (o->count == 0) {
            o->owner = 0;
            pthread_cond_broadcast(&o->cv);
        }
    }
    pthread_mutex_unlock(&o->mu);
    return TRUE_;
}
HLE(CreateSemaphoreA) {   /* (sa, initial, max, name) */
    uint32_t ex = existing(H_SEMAPHORE, a[3]);
    if (ex) return ex;
    uint32_t h = h_new(H_SEMAPHORE);
    hobj_t *o = h_get(h, H_SEMAPHORE);
    o->count = (int)a[1];
    o->max_count = (int)a[2];
    return named(h, a[3]);
}
HLE(ReleaseSemaphore) {   /* (h, count, prev) */
    hobj_t *o = h_get(a[0], H_SEMAPHORE);
    if (!o) return set_err(ERROR_INVALID_HANDLE_);
    pthread_mutex_lock(&o->mu);
    if (a[2]) wr32(a[2], (uint32_t)o->count);
    o->count += (int)a[1];
    if (o->max_count && o->count > o->max_count) o->count = o->max_count;
    pthread_cond_broadcast(&o->cv);
    pthread_mutex_unlock(&o->mu);
    return TRUE_;
}

HLE(WaitForSingleObject) {
    hobj_t *o = h_get(a[0], H_FREE);
    if (!o) {
        rt_log("WaitForSingleObject: bad handle 0x%08X", a[0]);
        return 0xFFFFFFFFu;
    }
    return h_wait(o, a[1]);
}

/* ---- interlocked (guest memory, host atomics) ------------------------------------------------ */
HLE(InterlockedIncrement) { return __atomic_add_fetch((uint32_t *)G2H(a[0]), 1u, __ATOMIC_SEQ_CST); }
HLE(InterlockedDecrement) { return __atomic_sub_fetch((uint32_t *)G2H(a[0]), 1u, __ATOMIC_SEQ_CST); }
HLE(InterlockedExchange) { return __atomic_exchange_n((uint32_t *)G2H(a[0]), a[1], __ATOMIC_SEQ_CST); }
