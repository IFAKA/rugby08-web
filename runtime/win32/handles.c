/* handles.c -- kernel object handle table + waiting. */
#include "win32.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#define MAX_HANDLES 16384
#define H_BASE 0x100u
static hobj_t *g_h[MAX_HANDLES];
static pthread_mutex_t g_hmu = PTHREAD_MUTEX_INITIALIZER;

uint32_t h_new(htype_t type) {
    hobj_t *o = calloc(1, sizeof *o);
    o->type = type;
    o->refs = 1;
    pthread_mutex_init(&o->mu, NULL);
    pthread_cond_init(&o->cv, NULL);
    pthread_mutex_lock(&g_hmu);
    for (uint32_t i = 1; i < MAX_HANDLES; i++) {
        if (!g_h[i]) {
            g_h[i] = o;
            pthread_mutex_unlock(&g_hmu);
            return H_BASE + 4 * i;
        }
    }
    pthread_mutex_unlock(&g_hmu);
    rt_fatal("handle table full");
}

hobj_t *h_get(uint32_t h, htype_t type) {
    if (h < H_BASE || (h - H_BASE) % 4) return NULL;
    uint32_t i = (h - H_BASE) / 4;
    if (i >= MAX_HANDLES) return NULL;
    pthread_mutex_lock(&g_hmu);
    hobj_t *o = g_h[i];
    pthread_mutex_unlock(&g_hmu);
    if (!o || (type != H_FREE && o->type != type)) return NULL;
    return o;
}

void h_close(uint32_t h) {
    if (h < H_BASE || (h - H_BASE) % 4) return;
    uint32_t i = (h - H_BASE) / 4;
    if (i >= MAX_HANDLES) return;
    pthread_mutex_lock(&g_hmu);
    hobj_t *o = g_h[i];
    g_h[i] = NULL;
    pthread_mutex_unlock(&g_hmu);
    (void)o;   /* objects may still be waited on by another thread: leak rather than race */
}

uint64_t rt_host_tid(void) { return (uint64_t)(uintptr_t)pthread_self(); }

static int try_acquire(hobj_t *o) {
    switch (o->type) {
    case H_EVENT:
        if (!o->signaled) return 0;
        if (!o->manual_reset) o->signaled = 0;
        return 1;
    case H_SEMAPHORE:
        if (o->count <= 0) return 0;
        o->count--;
        return 1;
    case H_MUTEX:
        if (o->count > 0 && o->owner != rt_host_tid()) return 0;
        o->owner = rt_host_tid();
        o->count++;
        return 1;
    default:            /* threads, processes, files: signaled = done */
        return o->signaled;
    }
}

uint32_t h_wait(hobj_t *o, uint32_t timeout_ms) {
    pthread_mutex_lock(&o->mu);
    if (timeout_ms == 0xFFFFFFFFu) {
        while (!try_acquire(o)) pthread_cond_wait(&o->cv, &o->mu);
    } else {
        struct timeval now;
        gettimeofday(&now, NULL);
        struct timespec dl;
        uint64_t ns = (uint64_t)now.tv_usec * 1000 + (uint64_t)timeout_ms * 1000000ull;
        dl.tv_sec = now.tv_sec + (time_t)(ns / 1000000000ull);
        dl.tv_nsec = (long)(ns % 1000000000ull);
        while (!try_acquire(o)) {
            if (pthread_cond_timedwait(&o->cv, &o->mu, &dl) == ETIMEDOUT && !try_acquire(o)) {
                pthread_mutex_unlock(&o->mu);
                return WAIT_TIMEOUT_;
            }
        }
    }
    pthread_mutex_unlock(&o->mu);
    return 0;
}

void h_signal(hobj_t *o) {
    pthread_mutex_lock(&o->mu);
    o->signaled = 1;
    pthread_cond_broadcast(&o->cv);
    pthread_mutex_unlock(&o->mu);
}
