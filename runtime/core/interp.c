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
#else
#include "interp_int.h"



static RECOMP_TLS uc_engine *t_uc;
RECOMP_TLS int t_stop;
RECOMP_TLS uint32_t t_fault_va, t_fault_kind;
RECOMP_TLS uint32_t t_ring[RING];
RECOMP_TLS unsigned t_ring_i;
static int g_trace = -1;


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

recomp_func_t native_at(uint32_t va) {
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

bool hook_unmapped(uc_engine *uc, uc_mem_type type, uint64_t addr, int size, int64_t v, void *ud) {
    (void)size; (void)v; (void)ud;
    if (type == UC_MEM_FETCH_UNMAPPED && addr >= RT_BRIDGE_HOST) t_stop = STOP_BRIDGE;
    else { t_stop = STOP_FAULT; t_fault_va = (uint32_t)addr; t_fault_kind = type; }
    uc_emu_stop(uc);
    return false;
}

void hook_intr(uc_engine *uc, uint32_t intno, void *ud) {
    (void)ud;
    t_stop = STOP_FAULT; t_fault_va = 0; t_fault_kind = 0x1000 + intno;
    uc_emu_stop(uc);
}

int hook_cpuid(uc_engine *uc, void *ud) {
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

void state_in(uc_engine *uc) {
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

void state_out(uc_engine *uc) {
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

// A Unicorn engine on the whole guest arena, with flat code/data segments and fs at this thread's TIB.
// Loading any selector makes Unicorn reload every segment cache from the GDT, so cs/ss/ds/es need real
// descriptors too: with only fs, cs got a zero limit and the first far-away `ret` raised #GP.
uc_engine *interp_engine_new(const char *who, uc_cb_hookcode_t block_hook, uc_cb_hookmem_t write_hook) {
    uc_engine *uc;
    uc_err e = uc_open(UC_ARCH_X86, UC_MODE_32, &uc);
    if (e) rt_fatal("%s: uc_open: %s", who, uc_strerror(e));
    if ((e = uc_mem_map_ptr(uc, 0, rt_guest_size, UC_PROT_ALL, G2H(0)))) rt_fatal("%s: map guest arena: %s", who, uc_strerror(e));
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
        if ((e = uc_reg_write(uc, segs[i].reg, &segs[i].sel))) rt_fatal("%s: segment %d: %s", who, i, uc_strerror(e));
    uc_hook h;
    uc_hook_add(uc, &h, UC_HOOK_BLOCK, block_hook, NULL, 1, 0);
    uc_hook_add(uc, &h, UC_HOOK_MEM_UNMAPPED, hook_unmapped, NULL, 1, 0);
    uc_hook_add(uc, &h, UC_HOOK_INTR, hook_intr, NULL, 1, 0);
    uc_hook_add(uc, &h, UC_HOOK_INSN, hook_cpuid, NULL, 1, 0, UC_X86_INS_CPUID);
    if (write_hook) uc_hook_add(uc, &h, UC_HOOK_MEM_WRITE, write_hook, NULL, 1, 0);
    return uc;
}

static uc_engine *engine(void) {
    if (t_uc) return t_uc;
    if (g_trace < 0) g_trace = getenv("RT_INTERP_TRACE") != NULL;
    return t_uc = interp_engine_new("interp", hook_block, NULL);
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
#endif
