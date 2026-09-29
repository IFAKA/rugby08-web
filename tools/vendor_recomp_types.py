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
# Deterministic CPU model on every host (see runtime/DESIGN.md "Lifter choice"): Pentium Pro,
# FPU+TSC+CX8+CMOV, deliberately NO MMX/SSE/SSE2 so CPU-dispatched code takes the x87/integer
# paths; tools/lift_ext.py lifts those extensions as #UD faults. Extended leaves: none (no 3DNow!).
cpuid_old = """#else
    (void)eax_val; (void)ebx_val; (void)ecx_val; (void)edx_val;
#endif"""
cpuid_new = """#else
    (void)ebx_val; (void)ecx_val; (void)edx_val;
    if (eax_val == 0) {          /* max leaf 1, "GenuineIntel" */
        g_eax = 1; g_ebx = 0x756E6547u; g_edx = 0x49656E69u; g_ecx = 0x6C65746Eu;
    } else if (eax_val == 1) {   /* Pentium Pro: family 6 model 1 stepping 9 */
        g_eax = 0x00000619u; g_ebx = 0; g_ecx = 0;
        g_edx = (1u<<0) | (1u<<4) | (1u<<8) | (1u<<15); /* FPU TSC CX8 CMOV(+FCMOV) */
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

n_cur = t.count("extern uint32_t g_cur_func;")
t = t.replace("extern uint32_t g_cur_func;", "extern RECOMP_TLS uint32_t g_cur_func;")

ext = """
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
