#include <string.h>
/* log.c -- rt_log (log file), rt_trace (log + stdout: what checks grep), rt_fatal. */
#include "rt.h"
uint32_t rt_ms(void);   /* runtime/win32/k32_sys.c */
#include <stdio.h>
#include <math.h>
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
    va_list ap;
    va_start(ap, fmt);
    vlog(rt_verbose ? stderr : NULL, fmt, ap);
    va_end(ap);
}

void rt_trace(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(stdout, fmt, ap);
    va_end(ap);
}

void rt_fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(stderr, fmt, ap);
    va_end(ap);
    rt_log("FATAL: g_cur_func=0x%08X esp=0x%08X", g_cur_func, g_esp);
    recomp_dump_trace("fatal");
    exit(3);
}
