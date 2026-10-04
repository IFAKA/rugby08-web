#!/usr/bin/env python3
"""Regenerate runtime/include/recomp/recomp_types.h from pcrecomp's copy, making the
per-thread CPU state thread-local (the game runs real threads; pcrecomp assumes one).
Deterministic text transform; prints one line."""
import pathlib, sys

root = pathlib.Path(__file__).resolve().parent.parent
src = root / "third_party/pcrecomp/runtime/recomp32/recomp_types.h"
dst = root / "runtime/include/recomp/recomp_types.h"
t = src.read_text()

subs = [
    ("extern uint32_t g_eax, g_ecx, g_edx, g_esp;", "extern RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;"),
    ("extern uint32_t g_ebx, g_esi, g_edi, g_ebp;", "extern RECOMP_TLS uint32_t g_ebx, g_esi, g_edi, g_ebp;"),
    ("extern double   g_st[8];", "extern RECOMP_TLS double   g_st[8];"),
    ("extern int      g_fp_top;", "extern RECOMP_TLS int      g_fp_top;"),
    ("extern uint16_t g_fpu_cw;", "extern RECOMP_TLS uint16_t g_fpu_cw;"),
    # weak tentative definitions do not mix with TLS on every target: define them in the runtime
    ("RECOMP_WEAK uint32_t g_flag_k, g_flag_a, g_flag_b, g_flag_cf;",
     "extern RECOMP_TLS uint32_t g_flag_k, g_flag_a, g_flag_b, g_flag_cf;"),
    ("extern uint32_t g_fs_base;", "extern RECOMP_TLS uint32_t g_fs_base;"),
    ("extern uint64_t g_mm[8];", "extern RECOMP_TLS uint64_t g_mm[8];"),
]
for a, b in subs:
    if t.count(a) != 1:
        sys.exit(f"vendor_recomp_types: pattern not found exactly once: {a!r}")
    t = t.replace(a, b)
# Deterministic CPU model on every host (see runtime/DESIGN.md "Lifter choice"): Pentium II class,
# FPU+TSC+CX8+CMOV+MMX, deliberately NO SSE/SSE2 so CPU-dispatched code takes the x87/integer
# paths; tools/lift_ext.py lifts those extensions as #UD faults. Extended leaves: none (no 3DNow!).
# MMX is in because the statically linked VP6 decoder assumes it on every x86 (its CPU check only
# looks for higher tiers) and has no reachable non-MMX path.
cpuid_old = """#else
    (void)eax_val; (void)ebx_val; (void)ecx_val; (void)edx_val;
#endif"""
cpuid_new = """#else
    (void)ebx_val; (void)ecx_val; (void)edx_val;
    if (eax_val == 0) {          /* max leaf 1, "GenuineIntel" */
        g_eax = 1; g_ebx = 0x756E6547u; g_edx = 0x49656E69u; g_ecx = 0x6C65746Eu;
    } else if (eax_val == 1) {   /* Pentium II: family 6 model 3 stepping 3 */
        g_eax = 0x00000633u; g_ebx = 0; g_ecx = 0;
        g_edx = (1u<<0) | (1u<<4) | (1u<<8) | (1u<<15) | (1u<<23); /* FPU TSC CX8 CMOV(+FCMOV) MMX */
    } else {
        g_eax = 0; g_ebx = 0; g_ecx = 0; g_edx = 0;
    }
#endif"""
if t.count(cpuid_old) != 1:
    sys.exit("vendor_recomp_types: CPUID_impl fallback not found exactly once")
t = t.replace(cpuid_old, cpuid_new)
# __rdtsc exists only on x86 hosts; elsewhere use a monotonic nanosecond clock (runtime/cpu_host.c)
t = t.replace("#ifdef _MSC_VER\n#include <intrin.h>\n#endif",
              "#ifdef _MSC_VER\n#include <intrin.h>\n#elif !defined(__i386__) && !defined(__x86_64__)\n"
              "uint64_t recomp_host_rdtsc(void);\n#define __rdtsc() recomp_host_rdtsc()\n#else\n"
              "#include <x86intrin.h>\n#endif", 1)

# Unresolved dispatch goes to recomp_miss() (runtime/core/cpu.c): recorded once per target in
# logs/icall_miss.txt, which tools/icall_seed.py feeds back into the lift catalog.
for a, b in (('fprintf(stderr, "ICALL: unresolved VA 0x%08X from 0x%08X\\n", _va, g_cur_func);',
              'recomp_miss(0, _va, g_cur_func);'),
             ('else { fprintf(stderr, "ITAIL: unresolved VA 0x%08X from 0x%08X\\n", _va, g_cur_func); }',
              'else { recomp_miss(1, _va, g_cur_func); }')):
    if t.count(a) != 1:
        sys.exit(f"vendor_recomp_types: miss pattern not found: {a!r}")
    t = t.replace(a, b)
t = t.replace("/* Lookup functions */", "void recomp_miss(int tail, uint32_t va, uint32_t from);\n/* Lookup functions */", 1)

# Guest C++ exceptions (runtime/win32/k32_eh.c). The guest SEH chain at fs:[0] and the CRT's own
# __CxxFrameHandler are faithful, so only the final jump to a catch's continuation needs the host: a
# lifted function that installs an EH frame runs as body(resume, pad) under a setjmp landing pad that
# records ebp when the frame goes in; the continuation jump longjmps to the pad whose ebp matches and
# the body resumes at that label (or tail-jumps there when another fragment holds it).
t = t.replace("/* Lookup functions */", """#include <setjmp.h>
typedef struct rt_ehpad { struct rt_ehpad *prev; jmp_buf jb; uint32_t ebp, resume; } rt_ehpad_t;
extern RECOMP_TLS rt_ehpad_t *g_eh_top;
extern RECOMP_TLS int g_eh_active;                 /* > 0 while RaiseException runs guest handlers */
extern RECOMP_TLS uint32_t g_eh_resume;            /* wrapper -> body: continuation VA, 0 on entry */
void rt_eh_jump(uint32_t va);                      /* longjmps to the pad of the frame g_ebp names, if any */
/* The body stays void(void) so its tail jumps remain musttail; the pad lives in this wrapper frame. */
#define RECOMP_EH_WRAP(body) do { \\
    rt_ehpad_t _p; _p.prev = g_eh_top; _p.ebp = 0; _p.resume = 0; g_eh_top = &_p; \\
    if (setjmp(_p.jb)) g_eh_resume = _p.resume; else g_eh_resume = 0; \\
    body(); \\
    g_eh_top = _p.prev; \\
} while (0)
#define RECOMP_EH_BODY_ENTER() uint32_t _eh_resume = g_eh_resume; rt_ehpad_t *_eh_pad = g_eh_top; g_eh_resume = 0
/* Lookup functions */""", 1)

# A jmp to another lifted function is a jump, not a call: x86 does not grow the stack, so neither may
# the host. As a plain call, a loop whose back-edge crosses an entry recursed once per iteration: the
# protector split the byte-copy loop at 0x71BA50 into 0x71BA6D/0x71BA76, and copying the 482616-byte
# motion sequence table overflowed the 64 MB host stack (play 2026-10-04 11:14). musttail makes every
# tail jump reuse the frame; REGS_IN then REGS_OUT after the callee was a no-op copy, so it is exact.
itail_old = "    if (_fn) { RECOMP_REGS_OUT(); _fn(); RECOMP_REGS_IN(); } \\\n    else if (_va == RECOMP_RETADDR)"
itail_new = "    if (_fn) { RECOMP_REGS_OUT(); RECOMP_TAIL_RETURN(_fn); } \\\n    else if (_va == RECOMP_RETADDR)"
if t.count(itail_old) != 1:
    sys.exit("vendor_recomp_types: RECOMP_ITAIL call not found exactly once")
t = t.replace(itail_old, itail_new)
# While an exception is dispatched, _JumpToContinuation's `jmp eax` (ebp already the catching frame's)
# lands in that frame's host landing pad instead (runtime/win32/k32_eh.c, RECOMP_EH_WRAP below).
itail_head = "#define RECOMP_ITAIL(target_va) do { \\\n    uint32_t _va = (uint32_t)(target_va); \\\n"
if t.count(itail_head) != 1:
    sys.exit("vendor_recomp_types: RECOMP_ITAIL head not found exactly once")
t = t.replace(itail_head, itail_head + "    if (g_eh_active) { RECOMP_REGS_OUT(); rt_eh_jump(_va); } \\\n")
t = t.replace("/* Indirect tail call (jmp through dispatch) */",
              "#if defined(__has_attribute) && __has_attribute(musttail) && (!defined(__wasm__) || defined(__wasm_tail_call__))\n"
              "#define RECOMP_TAIL_RETURN(fn) __attribute__((musttail)) return (fn)()\n"
              "#else   /* no guaranteed tail calls: the old nested call (stack grows per tail jump) */\n"
              "#define RECOMP_TAIL_RETURN(fn) do { (fn)(); RECOMP_REGS_IN(); } while (0)\n"
              "#endif\n"
              "/* Indirect tail call (jmp through dispatch) */", 1)

n_cur = t.count("extern uint32_t g_cur_func;")
t = t.replace("extern uint32_t g_cur_func;", "extern RECOMP_TLS uint32_t g_cur_func;")

ext = """
/* ---- MMX instructions lift32 leaves out (tools/lift_ext.py MMX_EXTRA) ---- */
#define MM_B(v, i)  ((int8_t)((uint64_t)(v) >> ((i) * 8)))
#define MM_UB(v, i) ((uint8_t)((uint64_t)(v) >> ((i) * 8)))
#define MM_UW(v, i) ((uint16_t)((uint64_t)(v) >> ((i) * 16)))
#define MM_PUT_B(i, x) ((uint64_t)(uint8_t)(x) << ((i) * 8))
static inline int8_t mmx_sat8(int32_t v) { return (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v); }
static inline uint8_t mmx_usat8(int32_t v) { return (uint8_t)(v > 255 ? 255 : v < 0 ? 0 : v); }
static inline uint16_t mmx_usat16(int32_t v) { return (uint16_t)(v > 65535 ? 65535 : v < 0 ? 0 : v); }
#define MMX_BYTES(name, expr) static inline uint64_t name(uint64_t a, uint64_t b) { \\
    uint64_t r = 0; for (int i = 0; i < 8; i++) r |= MM_PUT_B(i, (expr)); return r; }
#define MMX_WORDS(name, expr) static inline uint64_t name(uint64_t a, uint64_t b) { \\
    uint64_t r = 0; for (int i = 0; i < 4; i++) r |= MM_PUT_W(i, (expr)); return r; }
#define MMX_DWORDS(name, expr) static inline uint64_t name(uint64_t a, uint64_t b) { \\
    uint64_t r = 0; for (int i = 0; i < 2; i++) r |= MM_PUT_D(i, (expr)); return r; }
MMX_BYTES(mmx_paddb, MM_B(a, i) + MM_B(b, i))
MMX_BYTES(mmx_psubb, MM_B(a, i) - MM_B(b, i))
MMX_BYTES(mmx_paddsb, mmx_sat8(MM_B(a, i) + MM_B(b, i)))
MMX_BYTES(mmx_psubsb, mmx_sat8(MM_B(a, i) - MM_B(b, i)))
MMX_BYTES(mmx_paddusb, mmx_usat8(MM_UB(a, i) + MM_UB(b, i)))
MMX_BYTES(mmx_psubusb, mmx_usat8(MM_UB(a, i) - MM_UB(b, i)))
MMX_BYTES(mmx_pcmpeqb, MM_B(a, i) == MM_B(b, i) ? 0xFF : 0)
MMX_BYTES(mmx_pcmpgtb, MM_B(a, i) > MM_B(b, i) ? 0xFF : 0)
MMX_WORDS(mmx_paddusw, mmx_usat16(MM_UW(a, i) + MM_UW(b, i)))
MMX_WORDS(mmx_psubusw, mmx_usat16(MM_UW(a, i) - MM_UW(b, i)))
MMX_DWORDS(mmx_pcmpeqd, MM_D(a, i) == MM_D(b, i) ? 0xFFFFFFFFu : 0)
MMX_DWORDS(mmx_pcmpgtd, MM_D(a, i) > MM_D(b, i) ? 0xFFFFFFFFu : 0)
static inline uint64_t mmx_packsswb(uint64_t a, uint64_t b) {   /* a's words -> low 4 bytes, b's -> high */
    uint64_t r = 0;
    for (int i = 0; i < 4; i++) r |= MM_PUT_B(i, mmx_sat8(MM_W(a, i))) | MM_PUT_B(i + 4, mmx_sat8(MM_W(b, i)));
    return r;
}

/* ---- tools/lift_ext.py support (implemented in runtime/core/cpu_ext.c) ---- */
void recomp_fault(uint32_t va, const char *what);   /* #UD/#GP/#BR/#OF: logs, aborts */
void recomp_soft_fault(uint32_t va, const char *what); /* #DE: logs once per site, continues */
void recomp_bcd_daa(uint32_t *eax, uint32_t *fl);
void recomp_bcd_das(uint32_t *eax, uint32_t *fl);
void recomp_bcd_aaa(uint32_t *eax, uint32_t *fl);
void recomp_bcd_aas(uint32_t *eax, uint32_t *fl);
void recomp_bcd_aam(uint32_t *eax, uint32_t *fl, uint32_t imm);
void recomp_bcd_aad(uint32_t *eax, uint32_t *fl, uint32_t imm);
void rt_hook_enter(uint32_t va, const char *name);   /* tools/hooks.json functions */
double recomp_fbld(uint32_t va);
void recomp_fbstp(uint32_t va, double v);
/* fist/fistp store: x86 writes the "integer indefinite" for NaN/out-of-range; a C cast is UB
   (arm64 and wasm saturate). Argument is already rounded by the control word. */
static inline uint16_t recomp_fist16(double r) { return (r >= -32768.0 && r < 32768.0) ? (uint16_t)(int16_t)r : 0x8000u; }
static inline uint32_t recomp_fist32(double r) { return (r >= -2147483648.0 && r < 2147483648.0) ? (uint32_t)(int32_t)r : 0x80000000u; }
static inline uint64_t recomp_fist64(double r) { return (r >= -9223372036854775808.0 && r < 9223372036854775808.0) ? (uint64_t)(int64_t)r : 0x8000000000000000ull; }
"""
i = t.rindex("#endif")
t = t[:i] + ext + t[i:]

header = ("/* GENERATED by tools/vendor_recomp_types.py from third_party/pcrecomp/runtime/recomp32/"
          "recomp_types.h -- do not edit; change the generator. Per-thread CPU state is RECOMP_TLS. */\n"
          "#include <stddef.h>\n#ifndef RECOMP_TLS\n#define RECOMP_TLS _Thread_local\n#endif\n")
dst.parent.mkdir(parents=True, exist_ok=True)
dst.write_text(header + t)
print(f"vendor_recomp_types: wrote {dst.relative_to(root)} ({len(subs)} decls + {n_cur} g_cur_func made TLS)")
