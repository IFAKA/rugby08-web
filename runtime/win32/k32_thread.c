/* k32_thread.c -- TLS slots, threads (a host pthread per guest thread, deterministic in RT_DET), Sleep. */
#include "win32.h"
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---- TLS: 64 slots in each thread's TEB page (TIB + 0xE10), as Windows lays them out -------- */
static uint64_t g_tls_used;
static pthread_mutex_t g_tlsmu = PTHREAD_MUTEX_INITIALIZER;
HLE(TlsAlloc) {
    (void)a;
    pthread_mutex_lock(&g_tlsmu);
    for (uint32_t i = 0; i < 64; i++) {
        if (!(g_tls_used & (1ull << i))) {
            g_tls_used |= 1ull << i;
            pthread_mutex_unlock(&g_tlsmu);
            return i;
        }
    }
    pthread_mutex_unlock(&g_tlsmu);
    return 0xFFFFFFFFu;
}
HLE(TlsFree) {
    pthread_mutex_lock(&g_tlsmu);
    int ok = a[0] < 64 && (g_tls_used & (1ull << a[0]));
    if (ok) {
        g_tls_used &= ~(1ull << a[0]);
        rt_thread_tls_set_all(a[0], 0);
    }   /* the next TlsAlloc of this slot starts at 0 */
    pthread_mutex_unlock(&g_tlsmu);
    return ok ? TRUE_ : set_err(ERROR_INVALID_PARAMETER_);
}
HLE(TlsGetValue) {
    rt_last_error = 0;
    return a[0] < 64 ? rd32(rt_thread_tib() + 0xE10 + 4 * a[0]) : 0;
}
HLE(TlsSetValue) {
    if (a[0] >= 64) return set_err(ERROR_INVALID_PARAMETER_);
    wr32(rt_thread_tib() + 0xE10 + 4 * a[0], a[1]);
    return TRUE_;
}

/* ---- threads ------------------------------------------------------------------------------------ */
#define THREAD_STACK (1u << 20)
typedef struct {
    uint32_t start, param, stack, tib, handle;
    int suspended;
    int det;                          /* det.c thread slot (deterministic mode) */
    pthread_t pt;
} gthread_t;
static int resumed(void *p) { return !((gthread_t *)p)->suspended; }
static RECOMP_TLS gthread_t *t_self;

static void *thread_main(void *arg) {
    gthread_t *t = arg;
    hobj_t *o = h_get(t->handle, H_THREAD);
    if (t->det >= 0) det_thread_begin(t->det);
    else {
        pthread_mutex_lock(&o->mu);
        while (t->suspended) pthread_cond_wait(&o->cv, &o->mu);
        pthread_mutex_unlock(&o->mu);
    }
    t_self = t;
    rt_thread_enter(t->stack, t->stack + THREAD_STACK, t->tib);
    uint32_t rc = guest_call(t->start, 1, t->param);
    o->exit_code = rc;
    h_signal(o);
    det_thread_end();
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
    t->det = det_on() ? det_thread_new(t->suspended ? resumed : NULL, t) : -1;
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
    if (t_self) {
        hobj_t *o = h_get(t_self->handle, H_THREAD);
        o->exit_code = a[0];
        h_signal(o);
    }
    det_thread_end();
    pthread_exit(NULL);
}
HLE(TerminateThread) {
    rt_log("TerminateThread(0x%08X) ignored", a[0]);
    return TRUE_;
}
HLE(GetExitCodeThread) {
    hobj_t *o = h_get(a[0], H_THREAD);
    if (!o) return set_err(ERROR_INVALID_HANDLE_);
    wr32(a[1], o->exit_code);
    return TRUE_;
}
HLE(GetCurrentThread) {
    (void)a;
    return 0xFFFFFFFEu;
}
HLE(GetCurrentThreadId) {
    (void)a;
    return rt_thread_tib() >> 12;
}
HLE(GetThreadPriority) {
    (void)a;
    return 0;
}
HLE(SetThreadPriority) {
    (void)a;
    return TRUE_;
}
HLE(DuplicateHandle) {   /* (srcproc, src, dstproc, &dst, access, inherit, options) */
    uint32_t h = a[1];
    if (h == 0xFFFFFFFEu && t_self) h = t_self->handle;   /* GetCurrentThread() pseudo-handle */
    uint32_t d = h_dup(h);
    if (!d) d = h;   /* a pseudo-handle (GetCurrentProcess) or a handle we do not track stays itself */
    if (a[3]) wr32(a[3], d);
    return TRUE_;
}

static void sleep_ms(uint32_t ms) {
    if (det_on() && det_thread_id() >= 0) {
        det_sleep_ms(ms);
        return;
    }
    if (!ms) {
        sched_yield();
        return;
    }
    struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
HLE(Sleep) {
    sleep_ms(a[0]);
    return 0;
}
HLE(SleepEx) {
    sleep_ms(a[0]);
    return 0;
}
