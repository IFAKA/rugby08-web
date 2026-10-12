/* shadow.c -- the shadow check (tools/hooks.json "shadow:NAME", RT_SHADOW_ALL): a function runs in the
 * interpreter first, its writes are undone, then the lifted body runs and both results are compared. */
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef RT_INTERP
void rt_shadow_enter(uint32_t va, const char *name) { (void)va; (void)name; }
recomp_func_t rt_shadow_lookup(uint32_t va) { (void)va; return NULL; }
void rt_shadow_auto(uint32_t va) { (void)va; }
#else
#include "../core/interp_int.h"

/* ---- shadow check (tools/hooks.json "shadow:NAME") ------------------------------------------------
 * At the function's entry the whole call tree runs in a second engine that never hands off to lifted
 * code, with every guest write logged (address, old byte). Its final values and registers are kept,
 * the writes are undone, and the function's return address is swapped for SHADOW_VA. The lifted body
 * then runs as usual; its `ret` reaches shadow_ret, which compares and returns to the real address.
 * Imports reached while interpreting run natively (their effects are not undone and run again in the
 * lifted pass), so diffs that trace back to an import are noise. The x87 model is 53-bit and the
 * reference 64-bit: float dwords within 1e-5 relative are counted as "fp-close", not reported.
 * RT_SHADOW_FROM=frame (default 0), RT_SHADOW_MAX=checks per function (default 20). */
#define SHADOW_VA 0xFFD00000u
typedef struct { uint32_t a, pc; uint8_t old; } undo_t;
typedef struct { uint32_t a, pc; uint8_t v; } fin_t;
typedef struct {
    const char *name; uint32_t va, ra, esp0, n, frame, bridges;
    uint32_t r[8]; double st0; int depth, top0, mark, st0_ok;
    fin_t *fin; size_t nfin;
} shadow_t;
static RECOMP_TLS uc_engine *t_suc;
static RECOMP_TLS undo_t *t_undo;
static RECOMP_TLS size_t t_nundo, t_cundo;
static RECOMP_TLS shadow_t t_sh;
static RECOMP_TLS int t_sh_busy;

static void undo_push(uint32_t a, uint32_t pc, uint8_t old) {
    if (t_nundo == t_cundo) { t_cundo = t_cundo ? 2 * t_cundo : 1 << 16; t_undo = realloc(t_undo, t_cundo * sizeof *t_undo); }
    t_undo[t_nundo++] = (undo_t){ a, pc, old };
}
static void hook_write(uc_engine *uc, uc_mem_type type, uint64_t addr, int size, int64_t v, void *ud) {
    (void)type; (void)v; (void)ud;
    uint32_t pc; uc_reg_read(uc, UC_X86_REG_EIP, &pc);
    for (int i = 0; i < size; i++)
        if (addr + i < rt_guest_size) undo_push((uint32_t)addr + i, pc, *(uint8_t *)G2H((uint32_t)addr + i));
}
static void hook_block_ring(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
    (void)uc; (void)size; (void)ud;
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

static void shadow_ret(void);

static int shadow_run(uint32_t va, const char *name, uint32_t nth, int autom);

void rt_entry_hooks_arm(void);
void rt_shadow_enter(uint32_t va, const char *name) {
    static int from = -1, max;
    rt_entry_hooks_arm();
    static struct { uint32_t va, n; } count[64]; static int ncount;
    if (from < 0) {
        from = getenv("RT_SHADOW_FROM") ? atoi(getenv("RT_SHADOW_FROM")) : 0;
        max = getenv("RT_SHADOW_MAX") ? atoi(getenv("RT_SHADOW_MAX")) : 20;
    }
    if (t_sh_busy && rt_present_frame > t_sh.frame + 2) t_sh_busy = 0;   /* a check that never returned (unwound) */
    if (t_sh_busy || (int)rt_present_frame < from) return;
    int ci = 0; while (ci < ncount && count[ci].va != va) ci++;
    if (ci == ncount) { if (ncount == 64) return; count[ncount++].va = va; }
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
static int shadow_run(uint32_t va, const char *name, uint32_t nth, int autom) {
    t_sh_busy = 1;

    uint32_t s_r[8] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi };
    uint32_t s_fk = g_flag_k, s_fa = g_flag_a, s_fb = g_flag_b, s_cf = g_flag_cf;
    int s_top = g_fp_top; uint16_t s_cw = g_fpu_cw; double s_st[8]; memcpy(s_st, (const void *)g_st, sizeof s_st);
    uint32_t esp0 = g_esp, ra0 = rd(esp0), caller = g_cur_func;

    uc_engine *uc = shadow_engine();
    uc_ctl_flush_tb(uc);
    /* MSVC calls enter with an empty x87 stack, except the CRT's _CI* helpers (acos, fmod, ...),
       which take their arguments in ST0/ST1. The lifted top is a running count that drifts, so
       the interpreter gets the top two registers at most, and the depth change is compared. */
    int crt = va >= 0x00871000u && va < 0x00890000u;   /* the statically linked MSVC CRT */
    int d0 = !crt || s_top < 0 ? 0 : s_top > 2 ? 2 : s_top;
    g_fp_top = d0;
    state_in(uc);
    g_fp_top = s_top;
    t_nundo = 0;
    uint32_t pc = va, bridges = 0;
    const char *fail = NULL;
    for (;;) {
        t_stop = STOP_NONE; t_fault_va = t_fault_kind = 0;
        uc_err e = uc_emu_start(uc, pc, ra0, 0, autom ? 2000000 : 200000000);
        uc_reg_read(uc, UC_X86_REG_EIP, &pc);
        if (t_stop == STOP_NONE && pc == ra0) break;
        if (t_stop == STOP_BRIDGE && autom) { fail = "import"; break; }
        if (t_stop == STOP_BRIDGE) {           /* an import: run it natively, as rt_interp does */
            state_out(uc);
            recomp_func_t fn = native_at(pc);
            if (!fn) { fail = "bridge without handler"; break; }
            uint32_t ra = rd(g_esp), slot = g_esp;
            wr_logged(slot, RECOMP_RETADDR);
            fn();
            g_cur_func = caller;
            if (rd(slot) == RECOMP_RETADDR) wr_logged(slot, ra);
            bridges++;
            state_in(uc);
            pc = ra;
            continue;
        }
        fail = t_stop == STOP_FAULT ? "fault" : e ? uc_strerror(e) : "runaway";
        break;
    }
    shadow_t sh = { name ? name : "", va, ra0, esp0, nth, rt_present_frame, bridges, { 0 }, 0.0, 0, s_top, 0, 0, NULL, 0 };
    if (!fail) {
        state_out(uc);
        uint32_t r[8] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi };
        memcpy(sh.r, r, sizeof r);
        sh.depth = g_fp_top - d0; sh.st0_ok = g_fp_top > 0; sh.st0 = g_fp_top > 0 ? g_st[0] : 0.0;
        sh.fin = malloc((t_nundo ? t_nundo : 1) * sizeof *sh.fin);
        size_t n = 0;          /* newest write first per address after a stable pass: walk the log backwards */
        for (size_t i = t_nundo; i-- > 0; ) sh.fin[n++] = (fin_t){ t_undo[i].a, t_undo[i].pc, *(uint8_t *)G2H(t_undo[i].a) };
        mergesort(sh.fin, n, sizeof *sh.fin, fin_cmp);
        for (size_t i = 0; i < n; i++)
            if (!sh.nfin || sh.fin[sh.nfin - 1].a != sh.fin[i].a) sh.fin[sh.nfin++] = sh.fin[i];
    }
    for (size_t i = t_nundo; i-- > 0; ) *(uint8_t *)G2H(t_undo[i].a) = t_undo[i].old;
    g_eax = s_r[0]; g_ecx = s_r[1]; g_edx = s_r[2]; g_ebx = s_r[3]; g_esp = s_r[4]; g_ebp = s_r[5]; g_esi = s_r[6]; g_edi = s_r[7];
    g_flag_k = s_fk; g_flag_a = s_fa; g_flag_b = s_fb; g_flag_cf = s_cf;
    g_fp_top = s_top; g_fpu_cw = s_cw; memcpy((void *)g_st, s_st, sizeof s_st);
    g_cur_func = caller;
    if (fail && autom) { t_sh_busy = 0; return 0; }
    if (fail) {
        char ring[RING * 9 + 1] = "", *p = ring;
        for (unsigned i = 0; i < 8; i++) {
            uint32_t a = t_ring[(t_ring_i - 8 + i) & (RING - 1)];
            if (a) p += sprintf(p, " %08X", a);
        }
        rt_trace("shadow %s #%u (frame %u): inconclusive, interpreter %s at %08X (access %08X); last blocks:%s",
                 name, nth, rt_present_frame, fail, pc, t_fault_va, ring);
        t_sh_busy = 0;
        return 1;
    }
    sh.mark = (int)rt_enter_mark();     /* the lifted run's callees, for the report */
    t_sh = sh;
    wr(esp0, SHADOW_VA);                    /* the lifted body's ret lands in shadow_ret */
    return 1;
}

recomp_func_t rt_shadow_lookup(uint32_t va) { return va == SHADOW_VA ? shadow_ret : NULL; }

static void shadow_ret(void) {
    shadow_t sh = t_sh;
    t_sh_busy = 0;
    static const char *rn[8] = { "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi" };
    uint32_t r[8] = { g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi };
    char regs[256] = "", *p = regs;
    for (int i = 0; i < 8; i++)
        if (r[i] != sh.r[i]) p += sprintf(p, " %s %08X/%08X", rn[i], r[i], sh.r[i]);
    /* modulo 8: x87 TOP is a ring. `ffree` tags a register empty without moving TOP (D3DX's matrix
       multiply ends each row with four of them), and the lifted top is an unwrapped count of it */
    if (((g_fp_top - sh.top0 - sh.depth) & 7) != 0) p += sprintf(p, " fpdepth %d/%d", g_fp_top - sh.top0, sh.depth);
    else if (sh.st0_ok) {   /* the interpreter ended with a value in ST0 (a result, or a _CI* return) */
        double a = g_st[0], b = sh.st0;
        if (!(a == b || fabs(a - b) <= 1e-9 * fabs(b) || (isnan(a) && isnan(b)))) p += sprintf(p, " st0 %.9g/%.9g", a, b);
    }
    /* memory the interpreter wrote, outside the dead stack below the entry esp */
    int ndiff = 0, close = 0, last = -1;
    for (size_t i = 0; i < sh.nfin; i++) {
        uint32_t a = sh.fin[i].a;
        if (a < sh.esp0 && a >= sh.esp0 - 0x40000) continue;
        if (a >= sh.esp0 && a < sh.esp0 + 4) continue;           /* the swapped return slot */
        if (*(uint8_t *)G2H(a) == sh.fin[i].v) continue;
        uint32_t d = a & ~3u;
        if ((int)d == last) continue;
        last = (int)d;
        uint8_t iv[4]; memcpy(iv, G2H(d), 4);                    /* interpreter's dword: its bytes over ours */
        for (size_t j = i; j < sh.nfin && sh.fin[j].a < d + 4; j++) iv[sh.fin[j].a - d] = sh.fin[j].v;
        for (size_t j = i; j-- > 0 && sh.fin[j].a >= d; ) iv[sh.fin[j].a - d] = sh.fin[j].v;
        float lf, inf; memcpy(&lf, G2H(d), 4); memcpy(&inf, iv, 4);
        if (isfinite(lf) && isfinite(inf) && fabsf(lf) > 1e-30f && fabsf(lf - inf) <= 1e-5f * fabsf(inf)) { close++; continue; }
        ndiff++;
        uint32_t iw; memcpy(&iw, iv, 4);
        if (ndiff <= 24) rt_trace("shadow sub_%08X %s #%u   [%08X] lifted %08X (%g) interp %08X (%g) written at %08X", sh.va, sh.name, sh.n, d, rd(d), lf, iw, inf, sh.fin[i].pc);
    }
    static unsigned checked, bad;
    checked++; if (regs[0] || ndiff) bad++;
    if (sh.name[0] || regs[0] || ndiff || checked % 500 == 0)
    if (g_cur_func != sh.va)        /* the lifted ret that came back here: normally the function's own */
        p += sprintf(p, " (returned from sub_%08X, entry esp %08X)", g_cur_func, sh.esp0);
    rt_trace("shadow sub_%08X %s #%u (frame %u): %s%s; %d memory diffs, %d fp-close, %zu bytes written, %u imports [%u checked, %u mismatched]",
             sh.va, sh.name, sh.n, sh.frame, (regs[0] || ndiff) ? "MISMATCH" : "match", regs, ndiff, close, sh.nfin, sh.bridges, checked, bad);
    if (regs[0] || ndiff) {
        uint32_t ent[24]; char list[24 * 9 + 1] = "", *q = list;
        int ne = rt_enter_since((uint32_t)sh.mark, ent, 24);
        for (int i = 0; i < ne; i++) q += sprintf(q, " %08X", ent[i]);
        rt_trace("shadow sub_%08X #%u   lifted run entered:%s", sh.va, sh.n, ne ? list : " (none)");
    }
    free(sh.fin);
    if (sh.ra == RECOMP_RETADDR) return;
    recomp_func_t fn = native_at(sh.ra);
    if (!fn) rt_fatal("shadow: no handler for the return address 0x%08X", sh.ra);
    fn();
}
#endif
