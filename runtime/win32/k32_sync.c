/* k32_sync.c -- critical sections, events, mutexes, semaphores, waits, interlocked ops,
 * TLS slots, threads, sleeping. Guest threads are host pthreads with their own guest stack/TIB. */
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
HLE(DeleteCriticalSection) { (void)a; return 0; }   /* keep the mutex: cheap, and avoids reuse races */
HLE(EnterCriticalSection) {
    pthread_mutex_lock(cs_get(a[0]));
    wr32(a[0] + 8, rd32(a[0] + 8) + 1);              /* RecursionCount */
    wr32(a[0] + 0xC, rt_thread_tib() >> 12);          /* OwningThread (our thread id) */
    return 0;
}
HLE(LeaveCriticalSection) {
    uint32_t r = rd32(a[0] + 8);
    if (r) wr32(a[0] + 8, r - 1);
    if (r <= 1) wr32(a[0] + 0xC, 0);
    pthread_mutex_unlock(cs_get(a[0]));
    return 0;
}

/* ---- events / mutexes / semaphores --------------------------------------------------------- */
static uint32_t named(uint32_t h, uint32_t name_va) {
    if (name_va) snprintf(h_get(h, H_FREE)->name, 260, "%s", gstr(name_va));
    return h;
}
HLE(CreateEventA) {   /* (sa, manual, initial, name) */
    uint32_t h = h_new(H_EVENT);
    hobj_t *o = h_get(h, H_EVENT);
    o->manual_reset = a[1] != 0;
    o->signaled = a[2] != 0;
    return named(h, a[3]);
}
HLE(SetEvent) { hobj_t *o = h_get(a[0], H_EVENT); if (!o) return set_err(ERROR_INVALID_HANDLE_); h_signal(o); return TRUE_; }
HLE(ResetEvent) {
    hobj_t *o = h_get(a[0], H_EVENT);
    if (!o) return set_err(ERROR_INVALID_HANDLE_);
    pthread_mutex_lock(&o->mu); o->signaled = 0; pthread_mutex_unlock(&o->mu);
    return TRUE_;
}
HLE(CreateMutexA) {   /* (sa, initialOwner, name) */
    uint32_t h = h_new(H_MUTEX);
    hobj_t *o = h_get(h, H_MUTEX);
    if (a[1]) { o->owner = rt_host_tid(); o->count = 1; }
    rt_last_error = 0;
    return named(h, a[2]);
}
HLE(ReleaseMutex) {
    hobj_t *o = h_get(a[0], H_MUTEX);
    if (!o) return set_err(ERROR_INVALID_HANDLE_);
    pthread_mutex_lock(&o->mu);
    if (o->count > 0 && o->owner == rt_host_tid() && --o->count == 0) { o->owner = 0; pthread_cond_broadcast(&o->cv); }
    pthread_mutex_unlock(&o->mu);
    return TRUE_;
}
HLE(CreateSemaphoreA) {   /* (sa, initial, max, name) */
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
    if (!o) { rt_log("WaitForSingleObject: bad handle 0x%08X", a[0]); return 0xFFFFFFFFu; }
    return h_wait(o, a[1]);
}

/* ---- interlocked (guest memory, host atomics) ------------------------------------------------ */
HLE(InterlockedIncrement) { return __atomic_add_fetch((uint32_t *)G2H(a[0]), 1u, __ATOMIC_SEQ_CST); }
HLE(InterlockedDecrement) { return __atomic_sub_fetch((uint32_t *)G2H(a[0]), 1u, __ATOMIC_SEQ_CST); }
HLE(InterlockedExchange) { return __atomic_exchange_n((uint32_t *)G2H(a[0]), a[1], __ATOMIC_SEQ_CST); }

/* ---- TLS: 64 slots in each thread's TEB page (TIB + 0xE10), as Windows lays them out -------- */
static uint64_t g_tls_used;
static pthread_mutex_t g_tlsmu = PTHREAD_MUTEX_INITIALIZER;
HLE(TlsAlloc) {
    (void)a;
    pthread_mutex_lock(&g_tlsmu);
    for (uint32_t i = 0; i < 64; i++) {
        if (!(g_tls_used & (1ull << i))) { g_tls_used |= 1ull << i; pthread_mutex_unlock(&g_tlsmu); return i; }
    }
    pthread_mutex_unlock(&g_tlsmu);
    return 0xFFFFFFFFu;
}
HLE(TlsFree) { pthread_mutex_lock(&g_tlsmu); if (a[0] < 64) g_tls_used &= ~(1ull << a[0]); pthread_mutex_unlock(&g_tlsmu); return TRUE_; }
HLE(TlsGetValue) { rt_last_error = 0; return a[0] < 64 ? rd32(rt_thread_tib() + 0xE10 + 4 * a[0]) : 0; }
HLE(TlsSetValue) { if (a[0] >= 64) return set_err(ERROR_INVALID_PARAMETER_); wr32(rt_thread_tib() + 0xE10 + 4 * a[0], a[1]); return TRUE_; }

/* ---- threads ------------------------------------------------------------------------------------ */
#define THREAD_STACK (1u << 20)
typedef struct {
    uint32_t start, param, stack, tib, handle;
    int suspended;
    pthread_t pt;
} gthread_t;
static RECOMP_TLS gthread_t *t_self;

static void *thread_main(void *arg) {
    gthread_t *t = arg;
    hobj_t *o = h_get(t->handle, H_THREAD);
    pthread_mutex_lock(&o->mu);
    while (t->suspended) pthread_cond_wait(&o->cv, &o->mu);
    pthread_mutex_unlock(&o->mu);
    t_self = t;
    rt_thread_enter(t->stack, t->stack + THREAD_STACK, t->tib);
    uint32_t rc = guest_call(t->start, 1, t->param);
    o->exit_code = rc;
    h_signal(o);
    return NULL;
}

HLE(CreateThread) {   /* (sa, stacksize, start, param, flags, &tid) */
    gthread_t *t = calloc(1, sizeof *t);
    t->start = a[2];
    t->param = a[3];
    t->stack = gmemalign(16, THREAD_STACK);
    t->tib = gmemalign(0x1000, 0x1000);
    t->suspended = (a[4] & 4) != 0;   /* CREATE_SUSPENDED */
    t->handle = h_new(H_THREAD);
    hobj_t *o = h_get(t->handle, H_THREAD);
    o->p = t;
    o->exit_code = 259;   /* STILL_ACTIVE */
    if (a[5]) wr32(a[5], t->tib >> 12);
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 8u << 20);   /* host stack: lifted code recurses deeply */
    if (pthread_create(&t->pt, &at, thread_main, t)) rt_fatal("CreateThread: pthread_create failed");
    pthread_detach(t->pt);
    rt_log("CreateThread(start=0x%08X, param=0x%08X) -> 0x%08X%s", t->start, t->param, t->handle,
           t->suspended ? " (suspended)" : "");
    return t->handle;
}
HLE(ResumeThread) {
    hobj_t *o = h_get(a[0], H_THREAD);
    if (!o || !o->p) return 0xFFFFFFFFu;
    gthread_t *t = o->p;
    pthread_mutex_lock(&o->mu);
    uint32_t prev = (uint32_t)t->suspended;
    t->suspended = 0;
    pthread_cond_broadcast(&o->cv);
    pthread_mutex_unlock(&o->mu);
    return prev;
}
HLE(ExitThread) {
    if (t_self) { hobj_t *o = h_get(t_self->handle, H_THREAD); o->exit_code = a[0]; h_signal(o); }
    pthread_exit(NULL);
}
HLE(TerminateThread) { rt_log("TerminateThread(0x%08X) ignored", a[0]); return TRUE_; }
HLE(GetExitCodeThread) { hobj_t *o = h_get(a[0], H_THREAD); if (!o) return set_err(ERROR_INVALID_HANDLE_); wr32(a[1], o->exit_code); return TRUE_; }
HLE(GetCurrentThread) { (void)a; return 0xFFFFFFFEu; }
HLE(GetCurrentThreadId) { (void)a; return rt_thread_tib() >> 12; }
HLE(GetThreadPriority) { (void)a; return 0; }
HLE(SetThreadPriority) { (void)a; return TRUE_; }
HLE(DuplicateHandle) {   /* (srcproc, src, dstproc, &dst, access, inherit, options) */
    uint32_t h = a[1];
    if (h == 0xFFFFFFFEu && t_self) h = t_self->handle;   /* GetCurrentThread() pseudo-handle */
    if (a[3]) wr32(a[3], h);
    return TRUE_;
}

static void sleep_ms(uint32_t ms) {
    if (!ms) { sched_yield(); return; }
    struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
HLE(Sleep) { sleep_ms(a[0]); return 0; }
HLE(SleepEx) { sleep_ms(a[0]); return 0; }
