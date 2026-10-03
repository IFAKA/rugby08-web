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
void recomp_trace_enter(uint32_t va) {
    uint32_t *e = t_enter[t_enter_idx++ & (ENTER_RING - 1)];
    e[0] = va; e[1] = g_ecx; e[2] = g_esp;
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

void rt_hook_enter(uint32_t va, const char *name) {
    rt_trace("%s entered (0x%08X)", name, va);
}
