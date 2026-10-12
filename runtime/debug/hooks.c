/* hooks.c -- tools/hooks.json: what a named function entry does. A plain name traces "<name> entered"
 * (checks grep for it); prefixes select a probe: args:, list:, ctl:, stick: (controller fields at their
 * offsets in the game's objects), shadow: (runtime/debug/shadow.c). */
#include "debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t rt_ms(void);   /* runtime/win32/k32_time.c */

/* tools/hooks.json: a name starting with "args:" logs the first three stack arguments and ecx
 * (the caller's state at entry: globals are current until the body's first REGS_OUT). */
/* controller manager (ecx): count +0xc, list +0x1c, each entry's type; once */
static void hook_list(const char *name) {
    static int done;
    if (done++) return;
    uint32_t n = rd32(g_ecx + 0xc), l = rd32(g_ecx + 0x1c);
    rt_trace("%s mgr=%08X count=%u list=%08X", name + 5, g_ecx, n, l);
    for (uint32_t i = 0; i <= n && i < 10 && l; i++) {
        uint32_t c = rd32(l + 4 * i);
        rt_trace("  [%u] %08X type=%u", i, c, c ? rd32(c + 0x2570) : 0);
    }
}

/* controller object (ecx): device type, selectable, active; on change */
static void hook_ctl(const char *name) {
    static uint32_t seen[16][4];
    static int n;
    uint32_t t = rd32(g_ecx + 0x2570), sel = rd32(g_ecx + 0x2580), act = rd32(g_ecx + 0x25a8);
    int i = 0;
    while (i < n && seen[i][0] != g_ecx) i++;
    if (i == n && n < 16) {
        seen[n][0] = g_ecx;
        seen[n][1] = ~0u;
        n++;
    }
    if (i < 16 && (seen[i][1] != t || seen[i][2] != sel || seen[i][3] != act)) {
        seen[i][1] = t;
        seen[i][2] = sel;
        seen[i][3] = act;
        rt_trace("%s ecx=%08X type=%u selectable=%u active=%u", name + 4, g_ecx, t, sel, act);
    }
}

/* controller (ecx) at its per-frame read: left stick x/y, 8-way dir; on change */
static void hook_stick(const char *name) {
    static uint32_t seen[16][5];
    static int n;
    {
        static uint32_t calls, ms0;   /* game frame time (app clock 0xA7ED28 +0x78) against host time */
        uint32_t clk = rd32(0xA7ED28);
        if (clk && ++calls % 120 == 0) {
            uint32_t raw = rd32(clk + 0x78), ms = rt_ms();
            float dt;
            memcpy(&dt, &raw, 4);
            rt_trace("%s clock dt=%g host %u ms per 120 reads", name + 6, dt, ms - ms0);
            ms0 = ms;
        }
    }
    uint32_t x = rd32(g_ecx + 8), y = rd32(g_ecx + 12), dir = rd32(g_ecx + 0x30), side = rd32(g_ecx + 0x2558);
    int i = 0;
    while (i < n && seen[i][0] != g_ecx) i++;
    if (i == n && n < 16) {
        seen[n][0] = g_ecx;
        seen[n][1] = ~0u;
        n++;
    }
    if (i < 16 && (seen[i][1] != x || seen[i][2] != y || seen[i][3] != dir || seen[i][4] != side)) {
        seen[i][1] = x;
        seen[i][2] = y;
        seen[i][3] = dir;
        seen[i][4] = side;
        float fx, fy;
        memcpy(&fx, &x, 4);
        memcpy(&fy, &y, 4);
        rt_trace("%s ecx=%08X type=%u side=%d x=%g y=%g dir=%u", name + 6, g_ecx, rd32(g_ecx + 0x2570), (int)side, fx,
                 fy, dir);
    }
}

static void hook_args(uint32_t va, const char *name) {
    rt_trace("%s (0x%08X) ecx=%08X args %08X %08X %08X", name + 5, va, g_ecx, rd32(g_esp + 4), rd32(g_esp + 8),
             rd32(g_esp + 12));
    {   // RT_ARGS_DUMP=N: also the first N dwords at ecx as floats (finding position/heading fields)
        static int n = -1;
        static uint32_t off;   // RT_ARGS_DUMP_OFF=hex: start the dump at ecx+off
        if (n < 0) {
            n = getenv("RT_ARGS_DUMP") ? atoi(getenv("RT_ARGS_DUMP")) : 0;
            off = getenv("RT_ARGS_DUMP_OFF") ? (uint32_t)strtoul(getenv("RT_ARGS_DUMP_OFF"), 0, 16) : 0;
        }
        if (n > 0 && g_ecx >= 0x10000) {
            char buf[4096];
            int k = 0;
            for (int i = 0; i < n && k < (int)sizeof buf - 32; i++) {
                uint32_t base = getenv("RT_ARGS_DUMP_DEREF")
                                    ? rd32(g_ecx + (uint32_t)strtoul(getenv("RT_ARGS_DUMP_DEREF"), 0, 16))
                                    : g_ecx;
                uint32_t v = rd32(base + off + 4u * (uint32_t)i);
                float f;
                memcpy(&f, &v, 4);
                k += getenv("RT_ARGS_DUMP_HEX") ? snprintf(buf + k, sizeof buf - k, " %08X", v)
                                                : snprintf(buf + k, sizeof buf - k, " %g", f);
            }
            rt_trace("%s dump f%u:%s", name + 5, rt_present_frame, buf);
        }
    }
}

void rt_hook_enter(uint32_t va, const char *name) {
    if (!strcmp(name, "WinMain") && getenv("RT_SELFTEST_CRT")) rt_selftest_crt();
    if (!strncmp(name, "list:", 5)) hook_list(name);
    else if (!strncmp(name, "ctl:", 4)) hook_ctl(name);
    else if (!strncmp(name, "shadow:", 7)) rt_shadow_enter(va, name + 7);
    else if (!strncmp(name, "stick:", 6)) hook_stick(name);
    else if (!strncmp(name, "args:", 5)) hook_args(va, name);
    else rt_trace("%s entered (0x%08X)", name, va);
}
