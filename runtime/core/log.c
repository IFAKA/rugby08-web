#include <string.h>
/* log.c -- rt_log (log file), rt_trace (log + stdout: what checks grep), rt_fatal. */
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

int rt_verbose;
static FILE *g_log;
static pthread_mutex_t g_log_mu = PTHREAD_MUTEX_INITIALIZER;

void rt_log_open(const char *path) {
    g_log = path ? fopen(path, "w") : NULL;
    if (!g_log) g_log = stderr;
}

static void vlog(FILE *also, const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    pthread_mutex_lock(&g_log_mu);
    FILE *out = g_log ? g_log : stderr;
    vfprintf(out, fmt, ap);
    fputc('\n', out);
    fflush(out);
    if (also && also != out) {
        vfprintf(also, fmt, ap2);
        fputc('\n', also);
        fflush(also);
    }
    pthread_mutex_unlock(&g_log_mu);
    va_end(ap2);
}

void rt_log(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vlog(rt_verbose ? stderr : NULL, fmt, ap); va_end(ap);
}

void rt_trace(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vlog(stdout, fmt, ap); va_end(ap);
}

void rt_fatal(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vlog(stderr, fmt, ap); va_end(ap);
    rt_log("FATAL: g_cur_func=0x%08X esp=0x%08X", g_cur_func, g_esp);
    recomp_dump_trace("fatal");
    exit(3);
}

/* RECOMP_TRACE builds: every lifted body calls this on entry (per-thread ring, dumped on fatal).
 * Each slot also keeps ecx and esp at entry (the registers are still in the globals there), so a
 * dump shows how `this` and the stack flowed into the fault. */
#define ENTER_RING 256
static RECOMP_TLS uint32_t t_enter[ENTER_RING][3];
static RECOMP_TLS uint32_t t_enter_idx;
/* RT_COVERAGE="from-to:file[,from-to:file...]" (presented frames): every lifted function entered
 * during a window is written to its file, one VA per line (rt_coverage_frame from d3d8 Present). */
static uint8_t *g_cov;              /* one bit per byte of [RT_IMAGE_BASE, RT_IMAGE_END) */
static volatile int g_cov_on;
void recomp_trace_enter(uint32_t va) {
    uint32_t *e = t_enter[t_enter_idx++ & (ENTER_RING - 1)];
    e[0] = va; e[1] = g_ecx; e[2] = g_esp;
    if (g_cov_on && va >= RT_IMAGE_BASE && va < RT_IMAGE_END) {
        uint32_t o = va - RT_IMAGE_BASE;
        g_cov[o >> 3] |= (uint8_t)(1u << (o & 7));
    }
}
void rt_coverage_frame(uint32_t frame) {
    static const char *spec; static int parsed;
    if (!parsed) { parsed = 1; spec = getenv("RT_COVERAGE"); }
    if (!spec) return;
    for (const char *p = spec; *p; ) {
        unsigned from = 0, to = 0; char file[256] = ""; int n = 0;
        if (sscanf(p, "%u-%u:%255[^,]%n", &from, &to, file, &n) < 3) return;
        if (frame == from) {
            size_t sz = (RT_IMAGE_END - RT_IMAGE_BASE + 7) / 8;
            if (!g_cov) g_cov = malloc(sz);
            memset(g_cov, 0, sz);
            g_cov_on = 1;
        }
        if (frame == to && g_cov) {
            g_cov_on = 0;
            FILE *f = fopen(file, "w");
            for (uint32_t o = 0; f && o < RT_IMAGE_END - RT_IMAGE_BASE; o++)
                if (g_cov[o >> 3] & (1u << (o & 7))) fprintf(f, "%08X\n", RT_IMAGE_BASE + o);
            if (f) fclose(f);
            rt_log("coverage %u-%u -> %s", from, to, file);
        }
        p += n; if (*p == ',') p++;
    }
}

void recomp_dump_trace(const char *why) {
    if (t_enter_idx) {
        rt_log("entered (%s), oldest first: va ecx esp", why);
        char line[16 * 32 + 1];
        int o = 0, k = 0;
        for (uint32_t i = t_enter_idx > ENTER_RING ? t_enter_idx - ENTER_RING : 0; i < t_enter_idx; i++) {
            const uint32_t *e = t_enter[i & (ENTER_RING - 1)];
            o += snprintf(line + o, sizeof line - (size_t)o, " %08X(%08X,%08X)", e[0], e[1], e[2]);
            if (++k % 16 == 0 || i + 1 == t_enter_idx) { rt_log(" %s", line); o = 0; }
        }
    }
    rt_log("icall trace (%s), last %d:", why, ICALL_TRACE_SIZE);
    for (int i = 0; i < ICALL_TRACE_SIZE; i++) {
        uint32_t k = (g_icall_trace_idx - ICALL_TRACE_SIZE + (uint32_t)i) & (ICALL_TRACE_SIZE - 1);
        if (g_icall_trace[k]) rt_log("  0x%08X <- 0x%08X", g_icall_trace[k], g_icall_from[k]);
    }
}

/* tools/hooks.json: a name starting with "args:" logs the first three stack arguments and ecx
 * (the caller's state at entry: globals are current until the body's first REGS_OUT). */
void rt_hook_enter(uint32_t va, const char *name) {
    if (!strncmp(name, "list:", 5)) {  /* controller manager (ecx): count +0xc, list +0x1c, each entry's type; once */
        static int done;
        if (done++) return;
        uint32_t n = rd32(g_ecx + 0xc), l = rd32(g_ecx + 0x1c);
        rt_trace("%s mgr=%08X count=%u list=%08X", name + 5, g_ecx, n, l);
        for (uint32_t i = 0; i <= n && i < 10 && l; i++) {
            uint32_t c = rd32(l + 4 * i);
            rt_trace("  [%u] %08X type=%u", i, c, c ? rd32(c + 0x2570) : 0);
        }
        return;
    }
    if (!strncmp(name, "ctl:", 4)) {   /* controller object (ecx): device type, selectable, active; on change */
        static uint32_t seen[16][4]; static int n;
        uint32_t t = rd32(g_ecx + 0x2570), sel = rd32(g_ecx + 0x2580), act = rd32(g_ecx + 0x25a8);
        int i = 0; while (i < n && seen[i][0] != g_ecx) i++;
        if (i == n && n < 16) { seen[n][0] = g_ecx; seen[n][1] = ~0u; n++; }
        if (i < 16 && (seen[i][1] != t || seen[i][2] != sel || seen[i][3] != act)) {
            seen[i][1] = t; seen[i][2] = sel; seen[i][3] = act;
            rt_trace("%s ecx=%08X type=%u selectable=%u active=%u", name + 4, g_ecx, t, sel, act);
        }
        return;
    }
    if (!strncmp(name, "args:", 5)) {
        rt_trace("%s (0x%08X) ecx=%08X args %08X %08X %08X", name + 5, va, g_ecx,
                 rd32(g_esp + 4), rd32(g_esp + 8), rd32(g_esp + 12));
        return;
    }
    rt_trace("%s entered (0x%08X)", name, va);
}
