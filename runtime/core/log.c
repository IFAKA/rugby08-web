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
    rt_shadow_auto(va);
    if (g_cov_on && va >= RT_IMAGE_BASE && va < RT_IMAGE_END) {
        uint32_t o = va - RT_IMAGE_BASE;
        g_cov[o >> 3] |= (uint8_t)(1u << (o & 7));
    }
}
volatile uint32_t rt_present_frame;
void rt_coverage_frame(uint32_t frame) {
    static const char *spec; static int parsed;
    rt_present_frame = frame;
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
/* RT_SELFTEST_CRT=1: at WinMain (the CRT is initialised) evaluate the game's own CRT math -- atan2
 * (0x873250) and exp (0x874C90), both through the CRT's fxam-classifying dispatcher, and _ftol
 * (0x872F18, every (int)float cast: fistp then an add/sbb fix-up to truncation) -- over a grid of
 * arguments, compare with the host's libm / C cast, print one line and exit (0 = all match). */
static double crt(uint32_t va, int n, double a, double b) {   /* cdecl double f(double[, double]) */
    uint32_t args[4];
    memcpy(&args[0], &a, 8); memcpy(&args[2], &b, 8);
    int top = g_fp_top;
    guest_callv(va, 2 * n, args);
    double r = g_fp_top > top ? g_st[0] : NAN;
    while (g_fp_top > top) {                 /* pop what the call left: the result */
        for (int i = 0; i < 7; i++) g_st[i] = g_st[i + 1];
        g_st[7] = 0.0; g_fp_top--;
    }
    return r;
}
static void selftest_crt(void) {
    static const double v[] = { 0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -3.0, 1e-3, 123.25, -0.70710678 };
    int n = 0, bad = 0;
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++)
        for (unsigned j = 0; j < sizeof v / sizeof *v; j++) {
            double y = v[i], x = v[j];
            double g = crt(0x873250u, 2, y, x), h = atan2(y, x);
            n++;
            if (!(g == h || fabs(g - h) <= 1e-12 * fabs(h))) {
                if (bad++ < 8) rt_trace("crt selftest: atan2(%g, %g) = %.17g, libm %.17g", y, x, g, h);
            }
        }
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
        double g = crt(0x874C90u, 1, v[i], 0), h = exp(v[i]);
        n++;
        if (!(g == h || fabs(g - h) <= 1e-12 * fabs(h)))
            if (bad++ < 8) rt_trace("crt selftest: exp(%g) = %.17g, libm %.17g", v[i], g, h);
    }
    static const double f[] = { 2.7, -2.7, 1.5, -1.5, 0.5, -0.5, 0.49, 3.0, -3.0, 123456.75, -123456.25, 1e-9 };
    for (unsigned i = 0; i < sizeof f / sizeof *f; i++) {     /* _ftol: argument in ST0, result edx:eax */
        int top = g_fp_top;
        for (int k = 7; k > 0; k--) g_st[k] = g_st[k - 1];
        g_st[0] = f[i]; g_fp_top++;
        int32_t g = (int32_t)guest_callv(0x872F18u, 0, NULL), h = (int32_t)f[i];
        while (g_fp_top > top) { for (int k = 0; k < 7; k++) g_st[k] = g_st[k + 1]; g_st[7] = 0.0; g_fp_top--; }
        n++;
        if (g != h && bad++ < 8) rt_trace("crt selftest: _ftol(%g) = %d, C cast %d", f[i], g, h);
    }
    rt_trace("crt selftest: %d cases, %d wrong -> %s", n, bad, bad ? "FAIL" : "PASS");
    exit(bad ? 1 : 0);
}

void rt_hook_enter(uint32_t va, const char *name) {
    if (!strcmp(name, "WinMain") && getenv("RT_SELFTEST_CRT")) selftest_crt();
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
    if (!strncmp(name, "shadow:", 7)) { rt_shadow_enter(va, name + 7); return; }
    if (!strncmp(name, "stick:", 6)) {  /* controller (ecx) at its per-frame read: left stick x/y, 8-way dir; on change */
        static uint32_t seen[16][5]; static int n;
        { static uint32_t calls, ms0;   /* game frame time (app clock 0xA7ED28 +0x78) against host time */
          uint32_t clk = rd32(0xA7ED28);
          if (clk && ++calls % 120 == 0) {
              uint32_t raw = rd32(clk + 0x78), ms = rt_ms(); float dt; memcpy(&dt, &raw, 4);
              rt_trace("%s clock dt=%g host %u ms per 120 reads", name + 6, dt, ms - ms0); ms0 = ms;
          } }
        uint32_t x = rd32(g_ecx + 8), y = rd32(g_ecx + 12), dir = rd32(g_ecx + 0x30), side = rd32(g_ecx + 0x2558);
        int i = 0; while (i < n && seen[i][0] != g_ecx) i++;
        if (i == n && n < 16) { seen[n][0] = g_ecx; seen[n][1] = ~0u; n++; }
        if (i < 16 && (seen[i][1] != x || seen[i][2] != y || seen[i][3] != dir || seen[i][4] != side)) {
            seen[i][1] = x; seen[i][2] = y; seen[i][3] = dir; seen[i][4] = side;
            float fx, fy; memcpy(&fx, &x, 4); memcpy(&fy, &y, 4);
            rt_trace("%s ecx=%08X type=%u side=%d x=%g y=%g dir=%u", name + 6, g_ecx, rd32(g_ecx + 0x2570), (int)side, fx, fy, dir);
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
