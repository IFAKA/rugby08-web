/* selftest_crt.c -- RT_SELFTEST_CRT=1 (feature lift.crt_math): the game's own CRT math against libm. */
#include "debug.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    memcpy(&args[0], &a, 8);
    memcpy(&args[2], &b, 8);
    int top = g_fp_top;
    uint32_t misses = recomp_miss_calls();
    guest_callv(va, 2 * n, args);
    crt_broke = guest_call_broke || recomp_miss_calls() != misses;
    double r = g_fp_top > top ? g_st[0] : NAN;
    while (g_fp_top > top) {                 /* pop what the call left: the result */
        for (int i = 0; i < 7; i++) g_st[i] = g_st[i + 1];
        g_st[7] = 0.0;
        g_fp_top--;
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
    if (crt_broke || (checked && !value_ok)) {
        if ((*bad)++ < 64) rt_trace("crt selftest: %s%s", what, crt_broke ? " (miss or convention broken)" : "");
        return 1;
    }
    if (!value_ok) rt_log("crt selftest: deviation (CRT special case) %s", what);
    return 0;
}
static const double k_v[] = { 0.0,         -0.0,   1.0,     -1.0,  0.5,    -0.5,   2.0,     -3.0,     1e-3,      123.25,
                              -0.70710678, 1000.0, -1000.0, 709.9, -745.2, 1e-310, -1e-310, INFINITY, -INFINITY, NAN };
#define NV (sizeof k_v / sizeof *k_v)

static void test_atan2(int *n, int *bad) {
    char what[160];
    for (unsigned i = 0; i < NV; i++)
        for (unsigned j = 0; j < NV; j++) {
            double y = k_v[i], x = k_v[j];
            double g = crt(0x873250u, 2, y, x), h = atan2(y, x);
            (*n)++;
            snprintf(what, sizeof what, "atan2(%g, %g) = %.17g, libm %.17g", y, x, g, h);
            crt_case(bad, same(g, h), !denormal(y) && !denormal(x) && !(isinf(y) && isinf(x)), what);
        }
}

static void test_exp_log(int *n, int *bad) {
    char what[160];
    static const struct {
        uint32_t va;
        const char *name;
        double (*libm)(double);
    } f1[] = {
        { 0x874C90u, "exp", exp },
        { 0x874E50u, "log", log },
    };
    for (unsigned k = 0; k < sizeof f1 / sizeof *f1; k++)
        for (unsigned i = 0; i < NV; i++) {
            double g = crt(f1[k].va, 1, k_v[i], 0), h = f1[k].libm(k_v[i]);
            (*n)++;
            snprintf(what, sizeof what, "%s(%g) = %.17g, libm %.17g", f1[k].name, k_v[i], g, h);
            crt_case(bad, same(g, h), !denormal(k_v[i]) && !(k == 0 && k_v[i] < -708.0 && k_v[i] > -INFINITY), what);
        }
}

static void test_ftol(int *n, int *bad) {
    static const double f[] = { 2.7, -2.7, 1.5, -1.5, 0.5, -0.5, 0.49, 3.0, -3.0, 123456.75, -123456.25, 1e-9 };
    for (unsigned i = 0; i < sizeof f / sizeof *f; i++) {     /* _ftol: argument in ST0, result edx:eax */
        int top = g_fp_top;
        for (int k = 7; k > 0; k--) g_st[k] = g_st[k - 1];
        g_st[0] = f[i];
        g_fp_top++;
        int32_t g = (int32_t)guest_callv(0x872F18u, 0, NULL), h = (int32_t)f[i];
        while (g_fp_top > top) {
            for (int k = 0; k < 7; k++) g_st[k] = g_st[k + 1];
            g_st[7] = 0.0;
            g_fp_top--;
        }
        (*n)++;
        if (g != h && (*bad)++ < 8) rt_trace("crt selftest: _ftol(%g) = %d, C cast %d", f[i], g, h);
    }
}

/* The lifter's inline _ftol2 (recomp_ftol2, tools/lift_ext.py FTOL2) against the lifted body
   itself: eax, ecx, edx and the stack depth, in every rounding mode, over halves, 2^31/2^32/2^63
   edges, infinities, NaN and a fixed pseudo-random spread. */
static void test_ftol2(int *n, int *bad) {
    double w[96];
    unsigned nw = 0;
    static const double e[] = { 0.0,
                                -0.0,
                                0.5,
                                -0.5,
                                0.49999999999,
                                1.5,
                                -1.5,
                                2.5,
                                -2.5,
                                1e-300,
                                -1e-300,
                                2147483647.5,
                                -2147483648.5,
                                4294967295.5,
                                4294967296.0,
                                -4294967296.5,
                                4294967296.25,
                                9007199254740993.0,
                                9.2233720368547748e18,
                                -9.2233720368547758e18,
                                9.3e18,
                                -9.3e18,
                                INFINITY,
                                -INFINITY,
                                NAN };
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
            g_st[0] = w[i];
            g_fp_top++;
            g_fpu_cw = cw;
            g_ecx = 0x22222222u;
            g_edx = 0x33333333u;
            uint32_t ga = guest_callv(0x872F18u, 0, NULL), gc = g_ecx, gd = g_edx;
            int popped = g_fp_top == top;
            while (g_fp_top > top) {
                for (int k = 0; k < 7; k++) g_st[k] = g_st[k + 1];
                g_st[7] = 0.0;
                g_fp_top--;
            }
            (*n)++;
            if ((ga != a || gc != c || gd != d || !popped) && (*bad)++ < 8)
                rt_trace("crt selftest: rc %u _ftol2(%.17g): lifted %08X:%08X ecx %08X%s, inline %08X:%08X ecx %08X",
                         rc, w[i], gd, ga, gc, popped ? "" : " (stack)", d, a, c);
        }
    g_fpu_cw = cw0;
}

void rt_selftest_crt(void) {
    int n = 0, bad = 0;
    test_atan2(&n, &bad);
    test_exp_log(&n, &bad);
    test_ftol(&n, &bad);
    test_ftol2(&n, &bad);
    /* _adjust_fdiv (0xAA40FC): the CRT's Pentium FDIV-bug test result. 1 sends every fmod/fprem
       through the software workaround _adj_fprem; any real or emulated modern x87 gives 0. */
    rt_trace("crt selftest: _adjust_fdiv = %u", rd32(0xAA40FCu));
    if (rd32(0xAA40FCu) != 0) bad++;
    rt_trace("crt selftest: %d cases, %d wrong -> %s", n, bad, bad ? "FAIL" : "PASS");
    exit(bad ? 1 : 0);
}
