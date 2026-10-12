/* shadow.c -- the shadow check (tools/hooks.json "shadow:NAME", RT_SHADOW_ALL): a function runs in the
 * interpreter first, its writes are undone, then the lifted body runs and both results are compared. */
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef RT_INTERP
void rt_shadow_enter(uint32_t va, const char *name) {
    (void)va;
    (void)name;
}
recomp_func_t rt_shadow_lookup(uint32_t va) {
    (void)va;
    return NULL;
}
void rt_shadow_auto(uint32_t va) { (void)va; }
#else
#include "shadow_int.h"

/* ---- shadow check (tools/hooks.json "shadow:NAME") ------------------------------------------------
 * At the function's entry the whole call tree runs in a second engine that never hands off to lifted
 * code, with every guest write logged (address, old byte). Its final values and registers are kept,
 * the writes are undone, and the function's return address is swapped for SHADOW_VA. The lifted body
 * then runs as usual; its `ret` reaches shadow_ret, which compares and returns to the real address.
 * Imports reached while interpreting run natively (their effects are not undone and run again in the
 * lifted pass), so diffs that trace back to an import are noise. The x87 model is 53-bit and the
 * reference 64-bit: float dwords within 1e-5 relative are counted as "fp-close", not reported.
 * RT_SHADOW_FROM=frame (default 0), RT_SHADOW_MAX=checks per function (default 20). */
static RECOMP_TLS uc_engine *t_suc;
static RECOMP_TLS undo_t *t_undo;
static RECOMP_TLS size_t t_nundo, t_cundo;
RECOMP_TLS shadow_t t_sh;
RECOMP_TLS int t_sh_busy;

static void undo_push(uint32_t a, uint32_t pc, uint8_t old) {
    if (t_nundo == t_cundo) {
        t_cundo = t_cundo ? 2 * t_cundo : 1 << 16;
        undo_t *p = realloc(t_undo, t_cundo * sizeof *t_undo);
        if (!p) rt_fatal("shadow: out of memory");
        t_undo = p;
    }
    t_undo[t_nundo++] = (undo_t){ a, pc, old };
}
static void hook_write(uc_engine *uc, uc_mem_type type, uint64_t addr, int size, int64_t v, void *ud) {
    (void)type;
    (void)v;
    (void)ud;
    uint32_t pc;
    uc_reg_read(uc, UC_X86_REG_EIP, &pc);
    for (int i = 0; i < size; i++)
        if (addr + i < rt_guest_size) undo_push((uint32_t)addr + i, pc, *(uint8_t *)G2H((uint32_t)addr + i));
}
static void hook_block_ring(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
    (void)uc;
    (void)size;
    (void)ud;
    t_ring[t_ring_i++ & (RING - 1)] = (uint32_t)addr;
}
static void wr_logged(uint32_t va, uint32_t v) {
    for (int i = 0; i < 4; i++) undo_push(va + i, 0, *(uint8_t *)G2H(va + i));
    wr(va, v);
}
static int fin_cmp(const void *x, const void *y) {   /* by address, then log order (pc slot reused) */
    const fin_t *a = x, *b = y;
    return a->a != b->a ? (a->a < b->a ? -1 : 1) : 0;
}

static uc_engine *shadow_engine(void) {
    if (!t_suc) t_suc = interp_engine_new("shadow", hook_block_ring, hook_write);
    return t_suc;
}

static int shadow_run(uint32_t va, const char *name, uint32_t nth, int autom);

void rt_entry_hooks_arm(void);
void rt_shadow_enter(uint32_t va, const char *name) {
    static int from = -1, max;
    rt_entry_hooks_arm();
    static struct {
        uint32_t va, n;
    } count[64];
    static int ncount;
    if (from < 0) {
        from = getenv("RT_SHADOW_FROM") ? atoi(getenv("RT_SHADOW_FROM")) : 0;
        max = getenv("RT_SHADOW_MAX") ? atoi(getenv("RT_SHADOW_MAX")) : 20;
    }
    if (t_sh_busy && rt_present_frame > t_sh.frame + 2) t_sh_busy = 0;   /* a check that never returned (unwound) */
    if (t_sh_busy || (int)rt_present_frame < from) return;
    int ci = 0;
    while (ci < ncount && count[ci].va != va) ci++;
    if (ci == ncount) {
        if (ncount == 64) return;
        count[ncount++].va = va;
    }
    if ((int)count[ci].n >= max) return;
    shadow_run(va, name, ++count[ci].n, 0);
}

/* RT_SHADOW_ALL="from-to" (presented frames): every lifted function entered in the window is checked
 * up to RT_SHADOW_MAX times (default 2), one at a time per thread. A check that reaches an import
 * (not run twice: Release, allocations...) or 2M instructions is dropped and the function is not
 * tried again, so later entries descend into its callees. Summary per function in the trace. */
static uint8_t *g_auto;            /* per image byte: checks done, 0xFF = skip */
static uint32_t g_auto_from, g_auto_to, g_auto_max = 2;
static uint32_t g_auto_lo, g_auto_hi = ~0u;      /* RT_SHADOW_RANGE=lo-hi (hex VAs): only functions in this range */
static int g_auto_parsed;
void rt_shadow_auto(uint32_t va) {
    if (!g_auto_parsed) {
        g_auto_parsed = 1;
        const char *e = getenv("RT_SHADOW_ALL");
        if (e && sscanf(e, "%u-%u", &g_auto_from, &g_auto_to) == 2) {
            g_auto = calloc(RT_IMAGE_END - RT_IMAGE_BASE, 1);
            if (getenv("RT_SHADOW_MAX")) g_auto_max = (uint32_t)atoi(getenv("RT_SHADOW_MAX"));
            if (getenv("RT_SHADOW_RANGE")) sscanf(getenv("RT_SHADOW_RANGE"), "%x-%x", &g_auto_lo, &g_auto_hi);
        }
    }
    if (!g_auto || va < RT_IMAGE_BASE || va >= RT_IMAGE_END) return;
    if (va < g_auto_lo || va > g_auto_hi) return;
    uint32_t f = rt_present_frame;
    if (f < g_auto_from || f > g_auto_to) return;
    if (t_sh_busy && f > t_sh.frame + 2) t_sh_busy = 0;
    if (t_sh_busy) return;
    uint8_t *st = &g_auto[va - RT_IMAGE_BASE];
    if (*st == 0xFF || *st >= g_auto_max) return;
    if (!shadow_run(va, NULL, ++*st, 1)) *st = 0xFF;
}

/* 1 = the check was armed (or reported); 0 = interpreter gave up (auto mode: skip this function) */
/* The guest CPU state a check saves before running the reference and puts back after. */
typedef struct {
    uint32_t r[8], fk, fa, fb, cf, cur;
    int top;
    uint16_t cw;
    double st[8];
} cpu_save_t;

static void cpu_save(cpu_save_t *c) {
    uint32_t r[8] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi };
    memcpy(c->r, r, sizeof r);
    c->fk = g_flag_k;
    c->fa = g_flag_a;
    c->fb = g_flag_b;
    c->cf = g_flag_cf;
    c->top = g_fp_top;
    c->cw = g_fpu_cw;
    memcpy(c->st, (const void *)g_st, sizeof c->st);
    c->cur = g_cur_func;
}

static void cpu_restore(const cpu_save_t *c) {
    g_eax = c->r[0];
    g_ecx = c->r[1];
    g_edx = c->r[2];
    g_ebx = c->r[3];
    g_esp = c->r[4];
    g_ebp = c->r[5];
    g_esi = c->r[6];
    g_edi = c->r[7];
    g_flag_k = c->fk;
    g_flag_a = c->fa;
    g_flag_b = c->fb;
    g_flag_cf = c->cf;
    g_fp_top = c->top;
    g_fpu_cw = c->cw;
    memcpy((void *)g_st, c->st, sizeof c->st);
    g_cur_func = c->cur;
}

/* The reference: interpret from va until it returns to ra0. Imports run natively (their writes logged,
 * so they are undone too) unless autom, where an import ends the check. NULL = it returned. */
static const char *interpret(uc_engine *uc, uint32_t va, uint32_t ra0, int autom, uint32_t caller, uint32_t *pc,
                             uint32_t *bridges) {
    *pc = va;
    for (;;) {
        t_stop = STOP_NONE;
        t_fault_va = t_fault_kind = 0;
        uc_err e = uc_emu_start(uc, *pc, ra0, 0, autom ? 2000000 : 200000000);
        uc_reg_read(uc, UC_X86_REG_EIP, pc);
        if (t_stop == STOP_NONE && *pc == ra0) return NULL;
        if (t_stop == STOP_BRIDGE && autom) return "import";
        if (t_stop != STOP_BRIDGE) return t_stop == STOP_FAULT ? "fault" : e ? uc_strerror(e) : "runaway";
        state_out(uc); /* an import: run it natively, as rt_interp does */
        recomp_func_t fn = native_at(*pc);
        if (!fn) return "bridge without handler";
        uint32_t ra = rd(g_esp), slot = g_esp;
        wr_logged(slot, RECOMP_RETADDR);
        fn();
        g_cur_func = caller;
        if (rd(slot) == RECOMP_RETADDR) wr_logged(slot, ra);
        (*bridges)++;
        state_in(uc);
        *pc = ra;
    }
}

/* The reference's final value of every byte it wrote, by address (newest write first per address). */
static void collect_writes(shadow_t *sh) {
    sh->fin = malloc((t_nundo ? t_nundo : 1) * sizeof *sh->fin);
    size_t n = 0; /* newest write first per address after a stable pass: walk the log backwards */
    for (size_t i = t_nundo; i-- > 0;)
        sh->fin[n++] = (fin_t){ t_undo[i].a, t_undo[i].pc, *(uint8_t *)G2H(t_undo[i].a) };
    mergesort(sh->fin, n, sizeof *sh->fin, fin_cmp);
    for (size_t i = 0; i < n; i++)
        if (!sh->nfin || sh->fin[sh->nfin - 1].a != sh->fin[i].a) sh->fin[sh->nfin++] = sh->fin[i];
}

static void report_inconclusive(const char *name, uint32_t nth, const char *fail, uint32_t pc) {
    char ring[RING * 9 + 1] = "", *p = ring;
    for (unsigned i = 0; i < 8; i++) {
        uint32_t a = t_ring[(t_ring_i - 8 + i) & (RING - 1)];
        if (a) p += sprintf(p, " %08X", a);
    }
    rt_trace("shadow %s #%u (frame %u): inconclusive, interpreter %s at %08X (access %08X); last blocks:%s", name, nth,
             rt_present_frame, fail, pc, t_fault_va, ring);
}

static int shadow_run(uint32_t va, const char *name, uint32_t nth, int autom) {
    t_sh_busy = 1;
    cpu_save_t saved;
    cpu_save(&saved);
    uint32_t esp0 = g_esp, ra0 = rd(esp0), caller = g_cur_func;

    uc_engine *uc = shadow_engine();
    uc_ctl_flush_tb(uc);
    /* MSVC calls enter with an empty x87 stack, except the CRT's _CI* helpers (acos, fmod, ...),
       which take their arguments in ST0/ST1. The lifted top is a running count that drifts, so
       the interpreter gets the top two registers at most, and the depth change is compared. */
    int crt = va >= 0x00871000u && va < 0x00890000u; /* the statically linked MSVC CRT */
    int d0 = !crt || saved.top < 0 ? 0 : saved.top > 2 ? 2 : saved.top;
    g_fp_top = d0;
    state_in(uc);
    g_fp_top = saved.top;
    t_nundo = 0;
    uint32_t pc, bridges = 0;
    const char *fail = interpret(uc, va, ra0, autom, caller, &pc, &bridges);
    shadow_t sh = { name ? name : "", va, ra0, esp0, nth, rt_present_frame, bridges, { 0 }, 0.0, 0,
                    saved.top,        0,  0,   NULL, 0 };
    if (!fail) {
        state_out(uc);
        uint32_t r[8] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi };
        memcpy(sh.r, r, sizeof r);
        sh.depth = g_fp_top - d0;
        sh.st0_ok = g_fp_top > 0;
        sh.st0 = g_fp_top > 0 ? g_st[0] : 0.0;
        collect_writes(&sh);
    }
    for (size_t i = t_nundo; i-- > 0;) *(uint8_t *)G2H(t_undo[i].a) = t_undo[i].old;
    cpu_restore(&saved);
    g_cur_func = caller;
    if (fail) {
        if (!autom) report_inconclusive(name, nth, fail, pc);
        t_sh_busy = 0;
        return autom ? 0 : 1;
    }
    sh.mark = (int)rt_enter_mark(); /* the lifted run's callees, for the report */
    t_sh = sh;
    wr(esp0, SHADOW_VA); /* the lifted body's ret lands in shadow_ret */
    return 1;
}

recomp_func_t rt_shadow_lookup(uint32_t va) { return va == SHADOW_VA ? shadow_ret : NULL; }
#endif
