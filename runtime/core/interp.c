/* interp.c -- dispatch-miss fallback: guest x86 that has no lifted body runs in Unicorn, on the same
 * guest memory, until control is back in lifted code.
 *
 * Entry (RECOMP_ICALL / RECOMP_ITAIL / guest_callv miss): [esp] holds RECOMP_RETADDR, either pushed
 * by the call or the return address of the lifted frame being tail-jumped out of. The interpreter
 * runs until a `ret` pops it (Unicorn `until` = RECOMP_RETADDR).
 *
 * Handoff to native: a block that starts a lifted body, with a return address at [esp] (RETADDR, or
 * an address right after a call instruction), is called natively instead. The real return address
 * is swapped for RECOMP_RETADDR so the native body returns here, and interpretation resumes at it.
 * Bridges (imports, COM methods; RT_BRIDGE_HOST and up, above the mapped arena) are reached by an
 * unmapped fetch and handed off the same way. A missed handoff only costs speed: interpreting a
 * lifted body gives the same result.
 *
 * State: g_eax..g_ebp, lazy flags (out as FK_EFLAGS), the x87 stack (g_st[i] = ST(i), g_fp_top =
 * depth) and the control word; fs is a GDT descriptor at the thread's TIB. CPUID answers as the
 * lifted code's CPUID_impl does. A fault inside interpreted code is fatal, with the last blocks.
 * RT_INTERP_TRACE=1 prints every interpreted block to stderr. */
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef RT_INTERP
int rt_interp(uint32_t va) { (void)va; return 0; }
void rt_shadow_enter(uint32_t va, const char *name) { (void)va; (void)name; }
recomp_func_t rt_shadow_lookup(uint32_t va) { (void)va; return NULL; }
void rt_shadow_auto(uint32_t va) { (void)va; }
#else
#include <unicorn/unicorn.h>

enum { STOP_NONE, STOP_NATIVE, STOP_BRIDGE, STOP_FAULT };
#define RING 32

typedef struct { uint64_t m; uint16_t e; } f80_t;   /* Unicorn's FPn layout: mantissa, then exponent */

static RECOMP_TLS uc_engine *t_uc;
static RECOMP_TLS int t_stop;
static RECOMP_TLS uint32_t t_fault_va, t_fault_kind;
static RECOMP_TLS uint32_t t_ring[RING];
static RECOMP_TLS unsigned t_ring_i;
static int g_trace = -1;

static uint32_t rd(uint32_t va) { uint32_t v; memcpy(&v, G2H(va), 4); return v; }
static void wr(uint32_t va, uint32_t v) { memcpy(G2H(va), &v, 4); }

/* A value at [esp] is a return address when the bytes before it end a call instruction. */
static int after_call(uint32_t ra) {
    if (ra == RECOMP_RETADDR) return 1;
    if (ra < RT_IMAGE_BASE + 8 || ra >= rt_guest_size) return 0;
    const uint8_t *p = G2H(ra);
    if (p[-5] == 0xE8) return 1;                                     /* call rel32 */
    static const int back[] = { 2, 3, 6, 7 };                       /* call r/m32 forms */
    for (int i = 0; i < 4; i++)
        if (p[-back[i]] == 0xFF && ((p[-back[i] + 1] >> 3) & 7) == 2) return 1;
    return 0;
}

static recomp_func_t native_at(uint32_t va) {
    recomp_func_t fn = recomp_lookup_manual(va);
    return fn ? fn : recomp_lookup(va);
}

static void hook_block(uc_engine *uc, uint64_t addr, uint32_t size, void *ud) {
    (void)size; (void)ud;
    t_ring[t_ring_i++ & (RING - 1)] = (uint32_t)addr;
    if (g_trace) {
        uint32_t e;
        uc_reg_read(uc, UC_X86_REG_ESP, &e);
        fprintf(stderr, "interp: block %08X esp %08X [esp] %08X\n", (uint32_t)addr, e, rd(e));
    }
    if (!recomp_lookup((uint32_t)addr)) return;
    uint32_t esp;
    uc_reg_read(uc, UC_X86_REG_ESP, &esp);
    if (!after_call(rd(esp))) return;
    t_stop = STOP_NATIVE;
    uc_emu_stop(uc);
}

static bool hook_unmapped(uc_engine *uc, uc_mem_type type, uint64_t addr, int size, int64_t v, void *ud) {
    (void)size; (void)v; (void)ud;
    if (type == UC_MEM_FETCH_UNMAPPED && addr >= RT_BRIDGE_HOST) t_stop = STOP_BRIDGE;
    else { t_stop = STOP_FAULT; t_fault_va = (uint32_t)addr; t_fault_kind = type; }
    uc_emu_stop(uc);
    return false;
}

static void hook_intr(uc_engine *uc, uint32_t intno, void *ud) {
    (void)ud;
    t_stop = STOP_FAULT; t_fault_va = 0; t_fault_kind = 0x1000 + intno;
    uc_emu_stop(uc);
}

static int hook_cpuid(uc_engine *uc, void *ud) {
    (void)ud;
    uint32_t a, b, c, d;
    uc_reg_read(uc, UC_X86_REG_EAX, &a);
    uc_reg_read(uc, UC_X86_REG_EBX, &b);
    uc_reg_read(uc, UC_X86_REG_ECX, &c);
    uc_reg_read(uc, UC_X86_REG_EDX, &d);
    CPUID_impl(a, b, c, d);
    uc_reg_write(uc, UC_X86_REG_EAX, &g_eax);
    uc_reg_write(uc, UC_X86_REG_EBX, &g_ebx);
    uc_reg_write(uc, UC_X86_REG_ECX, &g_ecx);
    uc_reg_write(uc, UC_X86_REG_EDX, &g_edx);
    return 1;
}

static f80_t to_f80(double d) {
    uint64_t b; memcpy(&b, &d, 8);
    f80_t r = { 0, (uint16_t)((b >> 48) & 0x8000) };
    uint32_t e = (b >> 52) & 0x7FF;
    uint64_t m = b & 0xFFFFFFFFFFFFFull;
    if (e == 0 && m == 0) return r;
    if (e == 0x7FF) { r.e |= 0x7FFF; r.m = 0x8000000000000000ull | (m << 11); return r; }
    if (e == 0) {                                  /* denormal: normalise into the wider exponent */
        int s = 0;
        while (!(m & 0x10000000000000ull)) { m <<= 1; s++; }
        r.e |= (uint16_t)(1 - 1023 + 16383 - s);
        r.m = m << 11;
        return r;
    }
    r.e |= (uint16_t)(e - 1023 + 16383);
    r.m = 0x8000000000000000ull | (m << 11);
    return r;
}

static double from_f80(f80_t f) {
    int sign = f.e >> 15, e = f.e & 0x7FFF;
    double d;
    if (e == 0x7FFF) {
        uint64_t b = ((uint64_t)sign << 63) | (0x7FFull << 52) | ((f.m << 1) >> 12);
        memcpy(&d, &b, 8);
        return d;
    }
    if (!f.m) return sign ? -0.0 : 0.0;
    d = __builtin_ldexp((double)f.m, e - 16383 - 63);   /* rounds the 64-bit mantissa to 53 bits */
    return sign ? -d : d;
}

static void state_in(uc_engine *uc) {
    uc_reg_write(uc, UC_X86_REG_EAX, &g_eax); uc_reg_write(uc, UC_X86_REG_ECX, &g_ecx);
    uc_reg_write(uc, UC_X86_REG_EDX, &g_edx); uc_reg_write(uc, UC_X86_REG_EBX, &g_ebx);
    uc_reg_write(uc, UC_X86_REG_ESP, &g_esp); uc_reg_write(uc, UC_X86_REG_EBP, &g_ebp);
    uc_reg_write(uc, UC_X86_REG_ESI, &g_esi); uc_reg_write(uc, UC_X86_REG_EDI, &g_edi);
    uint32_t ef = recomp_eflags(g_flag_k, g_flag_a, g_flag_b, g_flag_cf, 1);
    uc_reg_write(uc, UC_X86_REG_EFLAGS, &ef);
    int depth = g_fp_top < 0 ? 0 : g_fp_top > 8 ? 8 : g_fp_top;
    uint32_t top = (8 - depth) & 7, tag = 0xFFFF, sw = top << 11, cw = g_fpu_cw;
    /* Empty registers are written too: QEMU does not fault on an x87 stack underflow but reads the
       register, which must then be what the lifted model reads there (g_st beyond the depth: 0). */
    for (int i = 0; i < 8; i++) {
        uint32_t phys = (top + i) & 7;
        f80_t f = to_f80(i < depth ? g_st[i] : 0.0);
        uc_reg_write(uc, UC_X86_REG_FP0 + phys, &f);
        if (i < depth) tag &= ~(3u << (2 * phys));
    }
    uc_reg_write(uc, UC_X86_REG_FPCW, &cw);
    uc_reg_write(uc, UC_X86_REG_FPSW, &sw);
    uc_reg_write(uc, UC_X86_REG_FPTAG, &tag);
}

static void state_out(uc_engine *uc) {
    uc_reg_read(uc, UC_X86_REG_EAX, &g_eax); uc_reg_read(uc, UC_X86_REG_ECX, &g_ecx);
    uc_reg_read(uc, UC_X86_REG_EDX, &g_edx); uc_reg_read(uc, UC_X86_REG_EBX, &g_ebx);
    uc_reg_read(uc, UC_X86_REG_ESP, &g_esp); uc_reg_read(uc, UC_X86_REG_EBP, &g_ebp);
    uc_reg_read(uc, UC_X86_REG_ESI, &g_esi); uc_reg_read(uc, UC_X86_REG_EDI, &g_edi);
    uint32_t ef = 0, sw = 0, cw = 0;
    uc_reg_read(uc, UC_X86_REG_EFLAGS, &ef);
    g_flag_k = FK_EFLAGS; g_flag_a = ef; g_flag_b = 0; g_flag_cf = ef & 1;
    uc_reg_read(uc, UC_X86_REG_FPSW, &sw);
    uc_reg_read(uc, UC_X86_REG_FPCW, &cw);
    g_fpu_cw = (uint16_t)cw;
    uint32_t top = (sw >> 11) & 7;
    int depth = (8 - top) & 7;     /* an 8-deep stack reads as 0: the lifted model never builds one */
    for (int i = 0; i < 8; i++) {
        f80_t f = { 0, 0 };
        if (i < depth) uc_reg_read(uc, UC_X86_REG_FP0 + ((top + i) & 7), &f);
        g_st[i] = i < depth ? from_f80(f) : 0.0;
    }
    g_fp_top = depth;
}

static uc_engine *engine(void) {
    if (t_uc) return t_uc;
    if (g_trace < 0) g_trace = getenv("RT_INTERP_TRACE") != NULL;
    uc_engine *uc;
    uc_err e = uc_open(UC_ARCH_X86, UC_MODE_32, &uc);
    if (e) rt_fatal("interp: uc_open: %s", uc_strerror(e));
    e = uc_mem_map_ptr(uc, 0, rt_guest_size, UC_PROT_ALL, G2H(0));
    if (e) rt_fatal("interp: map guest arena: %s", uc_strerror(e));
    /* Segments: flat 4 GiB code and data, and fs at this thread's TIB. Loading any selector makes
       Unicorn reload every segment cache from the GDT, so cs/ss/ds/es need real descriptors too:
       with only fs, cs got a zero limit and the first far-away `ret` raised #GP. */
    uint32_t gdt = gcalloc(32), base = g_fs_base;
    uint64_t t[4] = { 0,
        0xFFFF | ((uint64_t)0x9A << 40) | ((uint64_t)0xF << 48) | ((uint64_t)0xC << 52),
        0xFFFF | ((uint64_t)0x92 << 40) | ((uint64_t)0xF << 48) | ((uint64_t)0xC << 52),
        0xFFF | ((uint64_t)(base & 0xFFFFFF) << 16) | ((uint64_t)0x92 << 40) | ((uint64_t)0x4 << 52)
              | ((uint64_t)(base >> 24) << 56) };
    memcpy(G2H(gdt), t, sizeof t);
    uc_x86_mmr gdtr = { 0, gdt, sizeof t - 1, 0 };
    uc_reg_write(uc, UC_X86_REG_GDTR, &gdtr);
    static const struct { int reg; uint32_t sel; } segs[] = {
        { UC_X86_REG_CS, 0x08 }, { UC_X86_REG_SS, 0x10 }, { UC_X86_REG_DS, 0x10 },
        { UC_X86_REG_ES, 0x10 }, { UC_X86_REG_FS, 0x18 } };
    for (int i = 0; i < 5; i++)
        if ((e = uc_reg_write(uc, segs[i].reg, &segs[i].sel))) rt_fatal("interp: segment %d: %s", i, uc_strerror(e));
    uc_hook h;
    uc_hook_add(uc, &h, UC_HOOK_BLOCK, hook_block, NULL, 1, 0);
    uc_hook_add(uc, &h, UC_HOOK_MEM_UNMAPPED, hook_unmapped, NULL, 1, 0);
    uc_hook_add(uc, &h, UC_HOOK_INTR, hook_intr, NULL, 1, 0);
    uc_hook_add(uc, &h, UC_HOOK_INSN, hook_cpuid, NULL, 1, 0, UC_X86_INS_CPUID);
    t_uc = uc;
    return uc;
}

static void fault(uint32_t entry, uint32_t pc, uc_err e) {
    char ring[RING * 11 + 1] = "", *p = ring;
    for (unsigned i = 0; i < RING; i++) {
        uint32_t a = t_ring[(t_ring_i + i) & (RING - 1)];
        if (a) p += sprintf(p, " %08X", a);
    }
    rt_fatal("interp: fault in code entered at 0x%08X: pc 0x%08X, %s 0x%08X (%s); last blocks:%s",
             entry, pc, t_fault_kind >= 0x1000 ? "interrupt" : "access", t_fault_kind >= 0x1000
             ? t_fault_kind - 0x1000 : t_fault_va, uc_strerror(e), ring);
}

int rt_interp(uint32_t va) {
    if (va < 0x10000u || va >= rt_guest_size) return 0;    /* not guest memory: the caller's miss path */
    uc_engine *uc = engine();
    uint32_t caller = g_cur_func;
    uc_ctl_flush_tb(uc);       /* every entry comes from native code, which may have rewritten code */
    state_in(uc);
    uint32_t pc = va;
    for (;;) {
        t_stop = STOP_NONE; t_fault_va = t_fault_kind = 0;   /* an error without a hook (#UD) reports 0 */
        uc_err e = uc_emu_start(uc, pc, RECOMP_RETADDR, 0, 0);
        uc_reg_read(uc, UC_X86_REG_EIP, &pc);
        if (t_stop == STOP_NONE && pc == RECOMP_RETADDR) break;
        if (t_stop != STOP_NATIVE && t_stop != STOP_BRIDGE) fault(va, pc, e);
        state_out(uc);
        recomp_func_t fn = native_at(pc);
        if (!fn) rt_fatal("interp: no handler for 0x%08X (entered at 0x%08X)", pc, va);
        uint32_t ra = rd(g_esp), slot = g_esp;
        if (ra != RECOMP_RETADDR) wr(slot, RECOMP_RETADDR);
        fn();                                               /* returns by popping RECOMP_RETADDR */
        g_cur_func = caller;
        /* the popped slot is dead stack now; hardware leaves the real return address there */
        if (ra != RECOMP_RETADDR && rd(slot) == RECOMP_RETADDR) wr(slot, ra);
        if (ra == RECOMP_RETADDR) return 1;                 /* that was the interpreted frame's return */
        state_in(uc);
        pc = ra;
    }
    state_out(uc);
    return 1;
}

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
    if (t_suc) return t_suc;
    uc_engine *keep = t_uc;            /* engine() builds into t_uc: build a fresh one, then swap */
    t_uc = NULL;
    uc_engine *uc;
    uc_err e = uc_open(UC_ARCH_X86, UC_MODE_32, &uc);
    if (e) rt_fatal("shadow: uc_open: %s", uc_strerror(e));
    if ((e = uc_mem_map_ptr(uc, 0, rt_guest_size, UC_PROT_ALL, G2H(0)))) rt_fatal("shadow: map: %s", uc_strerror(e));
    uint32_t gdt = gcalloc(32), base = g_fs_base;
    uint64_t t[4] = { 0,
        0xFFFF | ((uint64_t)0x9A << 40) | ((uint64_t)0xF << 48) | ((uint64_t)0xC << 52),
        0xFFFF | ((uint64_t)0x92 << 40) | ((uint64_t)0xF << 48) | ((uint64_t)0xC << 52),
        0xFFF | ((uint64_t)(base & 0xFFFFFF) << 16) | ((uint64_t)0x92 << 40) | ((uint64_t)0x4 << 52)
              | ((uint64_t)(base >> 24) << 56) };
    memcpy(G2H(gdt), t, sizeof t);
    uc_x86_mmr gdtr = { 0, gdt, sizeof t - 1, 0 };
    uc_reg_write(uc, UC_X86_REG_GDTR, &gdtr);
    static const struct { int reg; uint32_t sel; } segs[] = {
        { UC_X86_REG_CS, 0x08 }, { UC_X86_REG_SS, 0x10 }, { UC_X86_REG_DS, 0x10 },
        { UC_X86_REG_ES, 0x10 }, { UC_X86_REG_FS, 0x18 } };
    for (int i = 0; i < 5; i++)
        if ((e = uc_reg_write(uc, segs[i].reg, &segs[i].sel))) rt_fatal("shadow: segment %d: %s", i, uc_strerror(e));
    uc_hook h;
    uc_hook_add(uc, &h, UC_HOOK_BLOCK, hook_block_ring, NULL, 1, 0);
    uc_hook_add(uc, &h, UC_HOOK_MEM_UNMAPPED, hook_unmapped, NULL, 1, 0);
    uc_hook_add(uc, &h, UC_HOOK_INTR, hook_intr, NULL, 1, 0);
    uc_hook_add(uc, &h, UC_HOOK_INSN, hook_cpuid, NULL, 1, 0, UC_X86_INS_CPUID);
    uc_hook_add(uc, &h, UC_HOOK_MEM_WRITE, hook_write, NULL, 1, 0);
    t_uc = keep;
    return t_suc = uc;
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
