/* com.c -- guest COM objects: a vtable of host bridges, and {vtbl, host index} objects (host pointers
 * are 64-bit natively, so they live in a side table). */
#include "rt.h"
#include "aems.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>

/* ---- COM ------------------------------------------------------------------ */
uint32_t rt_com_vtable(const char *iface, const rt_method_t *m, int n) {
    (void)iface;
    uint32_t vt = gcalloc(4u * (uint32_t)n);
    for (int i = 0; i < n; i++) wr32(vt + 4u * (uint32_t)i, rt_host_bridge(m[i].name, m[i].fn, m[i].pop));
    return vt;
}

/* Host pointers are 64-bit natively: keep them in a side table, store the index at this+4. */
#define MAX_COM_OBJS 65536
static void *g_com_host[MAX_COM_OBJS];
static uint32_t g_com_guest[MAX_COM_OBJS];
static uint32_t g_com_free[MAX_COM_OBJS];
static uint32_t g_com_count = 1;
static uint32_t g_com_free_count;
static pthread_mutex_t g_com_mu = PTHREAD_MUTEX_INITIALIZER;

uint32_t rt_com_new(uint32_t vtbl, void *host) {
    uint32_t obj = gcalloc(16);
    pthread_mutex_lock(&g_com_mu);
    uint32_t idx;
    if (g_com_free_count) {
        idx = g_com_free[--g_com_free_count];
    } else {
        if (g_com_count >= MAX_COM_OBJS) rt_fatal("COM object table full");
        idx = g_com_count++;
    }
    g_com_host[idx] = host;
    g_com_guest[idx] = obj;
    pthread_mutex_unlock(&g_com_mu);
    wr32(obj, vtbl);
    wr32(obj + 4, idx);
    return obj;
}

void *rt_com_host(uint32_t this_va) {
    uint32_t idx = rd32(this_va + 4);
    pthread_mutex_lock(&g_com_mu);
    void *host = idx < MAX_COM_OBJS && g_com_guest[idx] == this_va ? g_com_host[idx] : NULL;
    pthread_mutex_unlock(&g_com_mu);
    return host;
}

void rt_com_free(uint32_t this_va) {
    uint32_t idx = rd32(this_va + 4);
    pthread_mutex_lock(&g_com_mu);
    if (idx < MAX_COM_OBJS && g_com_guest[idx] == this_va) {
        g_com_host[idx] = NULL;
        g_com_guest[idx] = 0;
        g_com_free[g_com_free_count++] = idx;
    }
    pthread_mutex_unlock(&g_com_mu);
}
