/* cpu.c -- the CPU state the lifted code reads, dispatch lookups, bridges, guest_call. */
#include "rt.h"
#include "aems.h"
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>

/* Per-thread CPU state (declared RECOMP_TLS in the vendored recomp_types.h) */
RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
RECOMP_TLS double   g_st[8];
RECOMP_TLS int      g_fp_top;
RECOMP_TLS uint16_t g_fpu_cw = 0x027F;
RECOMP_TLS uint32_t g_flag_k, g_flag_a, g_flag_b, g_flag_cf;
RECOMP_TLS uint32_t g_fs_base;
RECOMP_TLS uint64_t g_mm[8];
RECOMP_TLS uint32_t g_cur_func;
RECOMP_TLS uint32_t rt_last_error;
uint16_t g_seg_cs, g_seg_ds, g_seg_es, g_seg_fs, g_seg_gs, g_seg_ss;
uint32_t g_gs_base;
ptrdiff_t g_mem_base;

/* Diagnostics rings: shared, racy by design (they only feed crash reports) */
uint32_t g_icall_trace[ICALL_TRACE_SIZE], g_icall_from[ICALL_TRACE_SIZE];
uint32_t g_icall_trace_idx, g_icall_count;

uint64_t recomp_host_rdtsc(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;  /* "1 GHz" TSC */
}

/* ---- bridges ------------------------------------------------------------ */
#define MAX_HOST_BRIDGES 8192
typedef struct { const char *name; rt_hle_fn fn; uint16_t pop; } host_bridge_t;
static host_bridge_t g_host[MAX_HOST_BRIDGES];
static uint32_t g_host_count;
static pthread_mutex_t g_host_mu = PTHREAD_MUTEX_INITIALIZER;

/* One trampoline per bridge kind: recover the index from the VA being dispatched.
 * RECOMP_ICALL has already pushed the dummy return address: a[k] = MEM32(esp + 4 + 4k). */
static RECOMP_TLS uint32_t t_bridge_va;

static void run_bridge(const char *dll, const char *name, rt_hle_fn fn, uint16_t pop) {
    const uint32_t *a = (const uint32_t *)G2H(g_esp + 4);
    uint32_t ret;
    if (fn) {
        ret = fn(a);
    } else {
        rt_log("UNIMPL %s!%s", dll, name);
        ret = 0;
    }
    g_eax = ret;
    g_esp += 4 + pop;          /* dummy return address + stdcall args */
}

static void import_trampoline(void) {
    uint32_t i = (t_bridge_va - RT_BRIDGE_IMPORT) >> 4;
    const rt_import_t *im = &rt_imports[i];
    run_bridge(im->dll, im->name, im->fn, im->pop);
}

static void host_trampoline(void) {
    uint32_t i = (t_bridge_va - RT_BRIDGE_HOST) >> 4;
    host_bridge_t *b = &g_host[i];
    run_bridge("host", b->name, b->fn, b->pop);
}

uint32_t rt_host_bridge(const char *name, rt_hle_fn fn, uint16_t pop) {
    pthread_mutex_lock(&g_host_mu);
    if (g_host_count >= MAX_HOST_BRIDGES) rt_fatal("host bridge table full");
    uint32_t i = g_host_count++;
    g_host[i] = (host_bridge_t){ name, fn, pop };
    pthread_mutex_unlock(&g_host_mu);
    return RT_BRIDGE_HOST + 16 * i;
}

recomp_func_t rt_shadow_lookup(uint32_t va);   /* interp.c: the shadow check's return trampoline */
recomp_func_t recomp_lookup_manual(uint32_t va) {
    if (va == 0xFFD00000u) return rt_shadow_lookup(va);
    if (va >= RT_BRIDGE_IMPORT && va < RT_BRIDGE_IMPORT + 16 * rt_import_count) {
        t_bridge_va = va;
        return import_trampoline;
    }
    if (va >= RT_BRIDGE_HOST && va < RT_BRIDGE_HOST + 16 * g_host_count) {
        t_bridge_va = va;
        return host_trampoline;
    }
    return NULL;
}

recomp_func_t recomp_lookup_import(uint32_t va) { (void)va; return NULL; }

recomp_func_t recomp_lookup(uint32_t va) {
    uint32_t lo = 0, hi = recomp_dispatch_count;
    while (lo < hi) {
        uint32_t mid = (lo + hi) >> 1;
        uint32_t m = recomp_dispatch_table[mid].address;
        if (m == va) return recomp_dispatch_table[mid].func;
        if (m < va) lo = mid + 1; else hi = mid;
    }
    /* outside the image: run-time loaded sound-bank code (runtime/core/aems.c) */
    return (va < RT_IMAGE_BASE || va >= RT_IMAGE_END) ? aems_lookup(va) : NULL;
}

/* ---- COM ------------------------------------------------------------------ */
uint32_t rt_com_vtable(const char *iface, const rt_method_t *m, int n) {
    (void)iface;
    uint32_t vt = gcalloc(4u * (uint32_t)n);
    for (int i = 0; i < n; i++)
        wr32(vt + 4u * (uint32_t)i, rt_host_bridge(m[i].name, m[i].fn, m[i].pop));
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

/* ---- guest_call ------------------------------------------------------------ */
uint32_t guest_callv(uint32_t va, int n, const uint32_t *args) {
    recomp_func_t fn = recomp_lookup_manual(va);
    if (!fn) fn = recomp_lookup(va);
    if (!fn && (va < 0x10000u || va >= rt_guest_size)) {
        rt_log("guest_call: no function at 0x%08X", va);
        return 0;
    }
    uint32_t sv_esp = g_esp, sv_ebx = g_ebx, sv_esi = g_esi, sv_edi = g_edi, sv_ebp = g_ebp;
    uint32_t sv_cur = g_cur_func;
    for (int i = n - 1; i >= 0; i--) { g_esp -= 4; wr32(g_esp, args[i]); }
    g_esp -= 4; wr32(g_esp, RECOMP_RETADDR);
    if (fn) fn();
    else { recomp_miss(0, va, sv_cur); rt_interp(va); }   /* no lifted body: interpret it */
    uint32_t ret = g_eax;
    g_esp = sv_esp; g_ebx = sv_ebx; g_esi = sv_esi; g_edi = sv_edi; g_ebp = sv_ebp;
    g_cur_func = sv_cur;
    return ret;
}

uint32_t guest_call(uint32_t va, int n, ...) {
    uint32_t args[16];
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n && i < 16; i++) args[i] = va_arg(ap, uint32_t);
    va_end(ap);
    return guest_callv(va, n, args);
}

/* ---- dispatch misses ---------------------------------------------------------------------------- */
#define MAX_MISSES 65536
static uint32_t g_miss[MAX_MISSES];
static int g_nmiss;
static pthread_mutex_t g_miss_mu = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_miss_f;

/* Code outside the image is loaded at run time (EA AEMS sound modules from Audio/aems.big). Its
 * surroundings are dumped to logs/dyncode/<va>.bin (u32 start, then 64 KiB of guest memory) so
 * tools/aems_capture.py can match the relocated module against the bank files. Native only. */
static void dyncode_capture(uint32_t va) {
#ifndef __EMSCRIPTEN__
    if (va < 0x10000u || (va >= RT_IMAGE_BASE && va < RT_IMAGE_END) || va >= RT_BRIDGE_HOST) return;
    uint32_t start = (va > 0x18000u ? va - 0x8000u : 0x10000u) & ~0xFFFu, n = 0x10000u;
    if (start + n > rt_guest_size) return;
    mkdir("logs/dyncode", 0755);
    char path[64];
    snprintf(path, sizeof path, "logs/dyncode/%08X.bin", va);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(&start, 4, 1, f);
    fwrite(G2H(start), 1, n, f);
    fclose(f);
#else
    (void)va;
#endif
}

void recomp_miss(int tail, uint32_t va, uint32_t from) {
    pthread_mutex_lock(&g_miss_mu);
    for (int i = 0; i < g_nmiss; i++) if (g_miss[i] == va) { pthread_mutex_unlock(&g_miss_mu); return; }
    if (g_nmiss < MAX_MISSES) g_miss[g_nmiss++] = va;
    if (!g_miss_f) g_miss_f = fopen("logs/icall_miss.txt", "w");
    if (g_miss_f) { fprintf(g_miss_f, "0x%08X %s from 0x%08X\n", va, tail ? "ITAIL" : "ICALL", from); fflush(g_miss_f); }
    pthread_mutex_unlock(&g_miss_mu);
    rt_log("%s: unresolved VA 0x%08X from 0x%08X", tail ? "ITAIL" : "ICALL", va, from);
    dyncode_capture(va);
}
