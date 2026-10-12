/* selftest_crt.c -- RT_SELFTEST_CRT=1 (feature lift.crt_math): the game's own CRT math against libm. */
#include "debug.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RT_SELFTEST_CRT=1: at WinMain (the CRT is initialised) evaluate the game's own CRT math -- atan2
 * (0x873250) and exp (0x874C90), both through the CRT's fxam-classifying dispatcher, and _ftol
 * (0x872F18, every (int)float cast: fistp then an add/sbb fix-up to truncation) -- over a grid of
 * arguments, compare with the host's libm / C cast, print one line and exit (0 = all match). */
static double crt(uint32_t va, int n, double a, double b) {   /* cdecl double f(double[, double]) */
    uint32_t args[4];
    memcpy(&args[0], &a, 8);
    memcpy(&args[2], &b, 8);
    int top = g_fp_top;
    guest_callv(va, 2 * n, args);
    double r = g_fp_top > top ? g_st[0] : NAN;
    while (g_fp_top > top) {                 /* pop what the call left: the result */
        for (int i = 0; i < 7; i++) g_st[i] = g_st[i + 1];
        g_st[7] = 0.0;
        g_fp_top--;
    }
    return r;
}
void rt_selftest_crt(void) {
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
        g_st[0] = f[i];
        g_fp_top++;
        int32_t g = (int32_t)guest_callv(0x872F18u, 0, NULL), h = (int32_t)f[i];
        while (g_fp_top > top) {
            for (int k = 0; k < 7; k++) g_st[k] = g_st[k + 1];
            g_st[7] = 0.0;
            g_fp_top--;
        }
        n++;
        if (g != h && bad++ < 8) rt_trace("crt selftest: _ftol(%g) = %d, C cast %d", f[i], g, h);
    }
    /* _adjust_fdiv (0xAA40FC): the CRT's Pentium FDIV-bug test result. 1 sends every fmod/fprem
       through the software workaround _adj_fprem; any real or emulated modern x87 gives 0. */
    rt_trace("crt selftest: _adjust_fdiv = %u", rd32(0xAA40FCu));
    if (rd32(0xAA40FCu) != 0) bad++;
    rt_trace("crt selftest: %d cases, %d wrong -> %s", n, bad, bad ? "FAIL" : "PASS");
    exit(bad ? 1 : 0);
}
