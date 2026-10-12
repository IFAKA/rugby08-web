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
volatile uint32_t rt_present_frame;
static RECOMP_TLS uint32_t t_enter[ENTER_RING][3];
RECOMP_TLS uint32_t t_enter_idx;   /* counted inline by RECOMP_ENTER (recomp_types.h) */
/* RT_COVERAGE="from-to:file[,from-to:file...]" (presented frames): every lifted function entered
 * during a window is written to its file, one VA per line (rt_coverage_frame from d3d8 Present). */
static uint8_t *g_cov;              /* one bit per byte of [RT_IMAGE_BASE, RT_IMAGE_END) */
static volatile int g_cov_on;
/* Functions entered on this thread since the index rt_enter_mark() returned (oldest first, up to
 * the ring's size) into out[]; returns how many. The shadow check names a mismatch's callees. */
uint32_t rt_enter_mark(void) { return t_enter_idx; }
int rt_enter_since(uint32_t mark, uint32_t *out, int max) {
    int n = 0;
    uint32_t from = t_enter_idx - mark > ENTER_RING ? t_enter_idx - ENTER_RING : mark;
    for (uint32_t i = from; i < t_enter_idx && n < max; i++) out[n++] = t_enter[i & (ENTER_RING - 1)][0];
    return n;
}
/* RT_PROBE="VA[:off,off..];VA..." (hex) with RT_PROBE_FRAMES="from-to": on every entry of VA in that
 * window, trace the frame, ecx, three stack arguments and the dwords at ecx+off (hex and as float).
 * Needs no relift: the entry hook of every lifted function calls here. */
static struct { uint32_t va; int n; uint32_t off[8]; } g_probe[8];
static int g_nprobe = -1;
static uint32_t g_probe_from, g_probe_to = ~0u;
static void probe_parse(void) {
    g_nprobe = 0;
    const char *e = getenv("RT_PROBE");
    if (getenv("RT_PROBE_FRAMES")) sscanf(getenv("RT_PROBE_FRAMES"), "%u-%u", &g_probe_from, &g_probe_to);
    while (e && *e && g_nprobe < 8) {
        char *end; uint32_t va = (uint32_t)strtoul(e, &end, 16);
        int k = g_nprobe++; g_probe[k].va = va; g_probe[k].n = 0;
        if (*end == ':') { e = end + 1; while (g_probe[k].n < 8) { g_probe[k].off[g_probe[k].n++] = (uint32_t)strtoul(e, &end, 16); e = end; if (*e != ',') break; e++; } }
        else e = end;
        if (*e == ';') e++; else break;
    }
}
/* RT_PROBE_DUMP="VA:arg:len" with RT_PROBE_FRAMES: the first entry of VA in a presented frame dumps len
 * bytes at the pointer in stack arg 0..2 (or ecx for arg 'e'): objects whose address varies per run. */
static void probe_hexdump(const char *tag, uint32_t f, uint32_t a, uint32_t len) {
    rt_trace("%s f%u at %08X", tag, f, a);
    for (uint32_t o = 0; o < len; o += 16) {
        uint32_t w[4]; float fl[4];
        for (int i = 0; i < 4; i++) { w[i] = rd32(a + o + 4u * (uint32_t)i); memcpy(&fl[i], &w[i], 4); }
        rt_trace("%s +%03X: %08X %08X %08X %08X | %g %g %g %g", tag, o, w[0], w[1], w[2], w[3], fl[0], fl[1], fl[2], fl[3]);
    }
}
/* RT_PROBE_DUMP="VA:arg:len[:len2:off,off,...]": also dump len2 bytes at each pointer found at obj+off. */
static void probe_dump(uint32_t va, uint32_t f) {
    static uint32_t dva, len, len2, offs[8], noff, last_f = ~0u; static int arg = -2;
    if (arg == -2) {
        const char *e = getenv("RT_PROBE_DUMP"); arg = -1;
        char c = 0; int n = 0;
        if (e && sscanf(e, "%x:%c:%x%n", &dva, &c, &len, &n) == 3) {
            arg = c == 'e' ? 3 : c - '0';
            if (e[n] == ':') { char *end; len2 = (uint32_t)strtoul(e + n + 1, &end, 16);
                while (*end == ':' || *end == ',') { offs[noff++ % 8] = (uint32_t)strtoul(end + 1, &end, 16); } }
        }
    }
    if (arg < 0 || va != dva || f == last_f || f < g_probe_from || f > g_probe_to || f % 5) return;
    last_f = f;
    uint32_t a = arg == 3 ? g_ecx : rd32(g_esp + 4 + 4u * (uint32_t)arg);
    probe_hexdump("dumpobj", f, a, len);
    for (uint32_t i = 0; i < noff && i < 8; i++) {
        uint32_t q = rd32(a + offs[i]);
        if (q > 0x10000 && len2) probe_hexdump("dumpent", f, q, len2);
    }
}
static void probe_enter(uint32_t va) {
    if (g_nprobe < 0) probe_parse();
    uint32_t f = rt_present_frame;
    probe_dump(va, f);
    if (f < g_probe_from || f > g_probe_to) return;
    for (int i = 0; i < g_nprobe; i++) if (g_probe[i].va == va) {
        char buf[400]; int o = snprintf(buf, sizeof buf, "probe %08X f%u ecx=%08X args %08X %08X %08X", va, f, g_ecx,
                                        rd32(g_esp + 4), rd32(g_esp + 8), rd32(g_esp + 12));
        for (int k = 0; k < g_probe[i].n && o < (int)sizeof buf - 40; k++) {
            uint32_t v = rd32(g_ecx + g_probe[i].off[k]); float fl; memcpy(&fl, &v, 4);
            o += snprintf(buf + o, sizeof buf - (size_t)o, " [%X]=%08X(%g)", g_probe[i].off[k], v, fl);
        }
        rt_trace("%s", buf);
    }
}
/* The debug machinery below (entry ring, shadow check, probes, coverage) runs on every function
 * entry, tens of thousands of times a frame; it costs ~20% of a match frame. It is armed once: by one
 * of these environment variables, or by the first rt_shadow_enter (a `shadow:` hook). */
int rt_entry_hooks = -1;
void rt_entry_hooks_arm(void) { rt_entry_hooks = 1; }
static void entry_hooks_init(void) {
    static const char *const v[] = { "RT_PROBE", "RT_PROBE_DUMP", "RT_SHADOW_ALL", "RT_SHADOW_FROM", "RT_COVERAGE" };
    rt_entry_hooks = 0;
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++) if (getenv(v[i])) rt_entry_hooks = 1;
}
void recomp_trace_enter(uint32_t va) {
    if (rt_entry_hooks <= 0) {
        if (rt_entry_hooks < 0) entry_hooks_init();
        if (!rt_entry_hooks) {
            if ((++t_enter_idx & 4095) == 0) rt_guest_yield();
            return;
        }
    }
    uint32_t *e = t_enter[t_enter_idx++ & (ENTER_RING - 1)];
    e[0] = va; e[1] = g_ecx; e[2] = g_esp;
    if ((t_enter_idx & 4095) == 0) rt_guest_yield();   /* cpu.c guest lock: let waiting threads run */
    rt_shadow_auto(va);
    probe_enter(va);
    if (g_cov_on && va >= RT_IMAGE_BASE && va < RT_IMAGE_END) {
        uint32_t o = va - RT_IMAGE_BASE;
        g_cov[o >> 3] |= (uint8_t)(1u << (o & 7));
    }
}
/* RT_DUMP="hexaddr:hexlen@frame[,...]": at that presented frame write the guest memory range to the
 * trace as one line per 16 bytes (hex dwords and the first float), for chasing object pointers. */
static void dump_at(uint32_t frame) {
    const char *e = getenv("RT_DUMP");
    while (e && *e) {
        char *end; uint32_t a = (uint32_t)strtoul(e, &end, 16), len = 0, fr = 0;
        if (*end == ':') len = (uint32_t)strtoul(end + 1, &end, 16);
        if (*end == '@') fr = (uint32_t)strtoul(end + 1, &end, 10);
        if (fr == frame && len) for (uint32_t o = 0; o < len; o += 16) {
            uint32_t w[4]; float f[4];
            for (int i = 0; i < 4; i++) { w[i] = rd32(a + o + 4u * (uint32_t)i); memcpy(&f[i], &w[i], 4); }
            rt_trace("dump f%u %08X: %08X %08X %08X %08X | %g %g %g %g", frame, a + o, w[0], w[1], w[2], w[3], f[0], f[1], f[2], f[3]);
        }
        e = end; if (*e == ',') e++; else break;
    }
}
void rt_coverage_frame(uint32_t frame) {
    static const char *spec; static int parsed;
    rt_present_frame = frame;
    dump_at(frame);
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
 * (0x873250), exp (0x874C90) and log (0x874E50), all through the CRT's fxam-classifying
 * dispatcher, and _ftol (0x872F18, every (int)float cast: fistp then an add/sbb fix-up to
 * truncation) -- over a grid of arguments, compare with the host's libm / C cast, print one line
 * and exit (0 = all match). The grid includes the argument classes the dispatcher sends to its
 * error arms (zero, negative, overflow, underflow, infinities, NaN, denormal): an arm that was never
 * lifted is a miss (natively the interpreter hides it; in the browser it is skipped, the stack is
 * left unbalanced and the game crashes later: exp at the first scrum, 2026-10-12), so a miss or a
 * call that breaks the calling convention fails the case. */
extern RECOMP_TLS int guest_call_broke;
uint32_t recomp_miss_calls(void);
static int crt_broke;   /* the last crt() call missed a lifted body or broke the convention */
static double crt(uint32_t va, int n, double a, double b) {   /* cdecl double f(double[, double]) */
    uint32_t args[4];
    memcpy(&args[0], &a, 8); memcpy(&args[2], &b, 8);
    int top = g_fp_top;
    uint32_t misses = recomp_miss_calls();
    guest_callv(va, 2 * n, args);
    crt_broke = guest_call_broke || recomp_miss_calls() != misses;
    double r = g_fp_top > top ? g_st[0] : NAN;
    while (g_fp_top > top) {                 /* pop what the call left: the result */
        for (int i = 0; i < 7; i++) g_st[i] = g_st[i + 1];
        g_st[7] = 0.0; g_fp_top--;
    }
    return r;
}
static int same(double g, double h) { return g == h || (g != g && h != h) || fabs(g - h) <= 1e-12 * fabs(h); }
static int denormal(double x) { return x != 0.0 && fabs(x) < 2.2250738585072014e-308; }
/* A case fails on a miss / broken convention (every argument), or on a value that differs from
 * libm where IEEE fixes the answer. Denormal arguments, atan2 of two infinities and exp underflow
 * go to the CRT's own special-case arms, whose exact results libm does not define for this CRT:
 * a difference there is reported as a deviation, not counted (same result on every host). */
static int crt_case(int *bad, int value_ok, int checked, const char *what) {
    if (crt_broke || (checked && !value_ok)) { if ((*bad)++ < 64) rt_trace("crt selftest: %s%s", what, crt_broke ? " (miss or convention broken)" : ""); return 1; }
    if (!value_ok) rt_log("crt selftest: deviation (CRT special case) %s", what);
    return 0;
}
static void selftest_crt(void) {
    static const double v[] = { 0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -3.0, 1e-3, 123.25, -0.70710678,
                                1000.0, -1000.0, 709.9, -745.2, 1e-310, -1e-310, INFINITY, -INFINITY, NAN };
    int n = 0, bad = 0;
    char what[160];
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++)
        for (unsigned j = 0; j < sizeof v / sizeof *v; j++) {
            double y = v[i], x = v[j];
            double g = crt(0x873250u, 2, y, x), h = atan2(y, x);
            n++;
            snprintf(what, sizeof what, "atan2(%g, %g) = %.17g, libm %.17g", y, x, g, h);
            crt_case(&bad, same(g, h), !denormal(y) && !denormal(x) && !(isinf(y) && isinf(x)), what);
        }
    static const struct { uint32_t va; const char *name; double (*libm)(double); } f1[] = {
        { 0x874C90u, "exp", exp }, { 0x874E50u, "log", log },
    };
    for (unsigned k = 0; k < sizeof f1 / sizeof *f1; k++)
        for (unsigned i = 0; i < sizeof v / sizeof *v; i++) {
            double g = crt(f1[k].va, 1, v[i], 0), h = f1[k].libm(v[i]);
            n++;
            snprintf(what, sizeof what, "%s(%g) = %.17g, libm %.17g", f1[k].name, v[i], g, h);
            crt_case(&bad, same(g, h), !denormal(v[i]) && !(k == 0 && v[i] < -708.0 && v[i] > -INFINITY), what);
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
    /* The lifter's inline _ftol2 (recomp_ftol2, tools/lift_ext.py FTOL2) against the lifted body
       itself: eax, ecx, edx and the stack depth, in every rounding mode, over halves, 2^31/2^32/2^63
       edges, infinities, NaN and a fixed pseudo-random spread. */
    {
        double w[96]; unsigned nw = 0;
        static const double e[] = { 0.0, -0.0, 0.5, -0.5, 0.49999999999, 1.5, -1.5, 2.5, -2.5, 1e-300, -1e-300,
            2147483647.5, -2147483648.5, 4294967295.5, 4294967296.0, -4294967296.5, 4294967296.25,
            9007199254740993.0, 9.2233720368547748e18, -9.2233720368547758e18, 9.3e18, -9.3e18,
            INFINITY, -INFINITY, NAN };
        for (unsigned i = 0; i < sizeof e / sizeof *e; i++) w[nw++] = e[i];
        uint32_t s = 12345u;
        while (nw < sizeof w / sizeof *w) {
            s = s * 1103515245u + 12345u;
            double m = (double)(int32_t)s / 65536.0;
            w[nw] = (nw & 1) ? m : m * 4096.0 * 1048576.0;
            nw++;
        }
        uint16_t cw0 = g_fpu_cw;
        for (unsigned rc = 0; rc < 4; rc++)
            for (unsigned i = 0; i < nw; i++) {
                uint16_t cw = (uint16_t)((cw0 & ~0x0C00u) | (rc << 10));
                uint32_t a = 0x11111111u, c = 0x22222222u, d = 0x33333333u;
                recomp_ftol2(w[i], cw, &a, &c, &d);
                int top = g_fp_top;
                for (int k = 7; k > 0; k--) g_st[k] = g_st[k - 1];
                g_st[0] = w[i]; g_fp_top++;
                g_fpu_cw = cw; g_ecx = 0x22222222u; g_edx = 0x33333333u;
                uint32_t ga = guest_callv(0x872F18u, 0, NULL), gc = g_ecx, gd = g_edx;
                int popped = g_fp_top == top;
                while (g_fp_top > top) { for (int k = 0; k < 7; k++) g_st[k] = g_st[k + 1]; g_st[7] = 0.0; g_fp_top--; }
                n++;
                if ((ga != a || gc != c || gd != d || !popped) && bad++ < 8)
                    rt_trace("crt selftest: rc %u _ftol2(%.17g): lifted %08X:%08X ecx %08X%s, inline %08X:%08X ecx %08X",
                             rc, w[i], gd, ga, gc, popped ? "" : " (stack)", d, a, c);
            }
        g_fpu_cw = cw0;
    }
    /* _adjust_fdiv (0xAA40FC): the CRT's Pentium FDIV-bug test result. 1 sends every fmod/fprem
       through the software workaround _adj_fprem; any real or emulated modern x87 gives 0. */
    rt_trace("crt selftest: _adjust_fdiv = %u", rd32(0xAA40FCu));
    if (rd32(0xAA40FCu) != 0) bad++;
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
        {   // RT_ARGS_DUMP=N: also the first N dwords at ecx as floats (finding position/heading fields)
            static int n = -1; static uint32_t off;   // RT_ARGS_DUMP_OFF=hex: start the dump at ecx+off
            if (n < 0) { n = getenv("RT_ARGS_DUMP") ? atoi(getenv("RT_ARGS_DUMP")) : 0;
                         off = getenv("RT_ARGS_DUMP_OFF") ? (uint32_t)strtoul(getenv("RT_ARGS_DUMP_OFF"), 0, 16) : 0; }
            if (n > 0 && g_ecx >= 0x10000) {
                char buf[4096]; int k = 0;
                for (int i = 0; i < n && k < (int)sizeof buf - 32; i++) {
                    uint32_t base = getenv("RT_ARGS_DUMP_DEREF") ? rd32(g_ecx + (uint32_t)strtoul(getenv("RT_ARGS_DUMP_DEREF"), 0, 16)) : g_ecx;
                    uint32_t v = rd32(base + off + 4u * (uint32_t)i); float f; memcpy(&f, &v, 4);
                    k += getenv("RT_ARGS_DUMP_HEX") ? snprintf(buf + k, sizeof buf - k, " %08X", v)
                                                    : snprintf(buf + k, sizeof buf - k, " %g", f);
                }
                rt_trace("%s dump f%u:%s", name + 5, rt_present_frame, buf);
            }
        }
        return;
    }
    rt_trace("%s entered (0x%08X)", name, va);
}
