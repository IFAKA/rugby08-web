/* trace.c -- what a crash report shows: the per-thread ring of function entries (RECOMP_TRACE builds)
 * and the indirect-call ring, and the entry hook that feeds the debugging tools (runtime/debug/). */
#include "rt.h"
#include "../debug/debug.h"
#include <stdio.h>
#include <stdlib.h>

/* RECOMP_TRACE builds: every lifted body calls this on entry (per-thread ring, dumped on fatal).
 * Each slot also keeps ecx and esp at entry (the registers are still in the globals there), so a
 * dump shows how `this` and the stack flowed into the fault. */
#define ENTER_RING 256
volatile uint32_t rt_present_frame;
static RECOMP_TLS uint32_t t_enter[ENTER_RING][3];
static RECOMP_TLS uint32_t t_enter_idx;

/* Functions entered on this thread since the index rt_enter_mark() returned (oldest first, up to
 * the ring's size) into out[]; returns how many. The shadow check names a mismatch's callees. */
uint32_t rt_enter_mark(void) { return t_enter_idx; }
int rt_enter_since(uint32_t mark, uint32_t *out, int max) {
    int n = 0;
    uint32_t from = t_enter_idx - mark > ENTER_RING ? t_enter_idx - ENTER_RING : mark;
    for (uint32_t i = from; i < t_enter_idx && n < max; i++) out[n++] = t_enter[i & (ENTER_RING - 1)][0];
    return n;
}

/* The debug machinery below (entry ring, shadow check, probes, coverage) runs on every function
 * entry, tens of thousands of times a frame; it costs ~20% of a match frame. It is armed once: by one
 * of these environment variables, or by the first rt_shadow_enter (a `shadow:` hook). */
int rt_entry_hooks = -1;
void rt_entry_hooks_arm(void) { rt_entry_hooks = 1; }
static void entry_hooks_init(void) {
    static const char *const v[] = { "RT_PROBE", "RT_PROBE_DUMP", "RT_SHADOW_ALL", "RT_SHADOW_FROM", "RT_COVERAGE" };
    rt_entry_hooks = 0;
    for (unsigned i = 0; i < sizeof v / sizeof *v; i++)
        if (getenv(v[i])) rt_entry_hooks = 1;
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
    e[0] = va;
    e[1] = g_ecx;
    e[2] = g_esp;
    if ((t_enter_idx & 4095) == 0) rt_guest_yield();   /* cpu.c guest lock: let waiting threads run */
    rt_shadow_auto(va);
    rt_probe_enter(va);
    rt_coverage_mark(va);
}

void recomp_dump_trace(const char *why) {
    if (t_enter_idx) {
        rt_log("entered (%s), oldest first: va ecx esp", why);
        char line[16 * 32 + 1];
        int o = 0, k = 0;
        for (uint32_t i = t_enter_idx > ENTER_RING ? t_enter_idx - ENTER_RING : 0; i < t_enter_idx; i++) {
            const uint32_t *e = t_enter[i & (ENTER_RING - 1)];
            o += snprintf(line + o, sizeof line - (size_t)o, " %08X(%08X,%08X)", e[0], e[1], e[2]);
            if (++k % 16 == 0 || i + 1 == t_enter_idx) {
                rt_log(" %s", line);
                o = 0;
            }
        }
    }
    rt_log("icall trace (%s), last %d:", why, ICALL_TRACE_SIZE);
    for (int i = 0; i < ICALL_TRACE_SIZE; i++) {
        uint32_t k = (g_icall_trace_idx - ICALL_TRACE_SIZE + (uint32_t)i) & (ICALL_TRACE_SIZE - 1);
        if (g_icall_trace[k]) rt_log("  0x%08X <- 0x%08X", g_icall_trace[k], g_icall_from[k]);
    }
}
