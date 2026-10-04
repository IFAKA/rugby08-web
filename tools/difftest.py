#!/usr/bin/env python3
"""Differential test of the lift on the game's own code: sampled leaf functions run twice from
identical state -- once in Unicorn (the x86 reference), once as our lifted C (tools/lift.py +
lift_ext, compiled with RECOMP_LOCAL_REGS like the real build) -- and every GPR, the x87 depth,
st(0), and hashes of .data, a scratch heap, the stack and every page Unicorn touched are compared.

Port of pcrecomp tools/lift/difftest.py's idea (which tests hand-written snippets) to whole
functions of this binary. Leaf = lifted body with no call/icall/tail/fault/cpuid/rdtsc; bodies
using transcendental x87 ops or 80-bit memory operands are skipped (the model is double
precision; libm vs softfloat differ in the last ulp) -- both by design, see runtime/DESIGN.md.
A case is EXCLUDED (not failed) when the reference itself faults (#DE, bad opcode) or runs away.

  tools/difftest.py --sample 500      exit 0 iff >= 500 compared and 0 mismatches
  tools/difftest.py --sample 20 --grep '_mm\['   only bodies using MMX
Prints one line; details -> logs/difftest.log.
"""
import argparse, json, os, pathlib, random, re, struct, subprocess, sys

root = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / "tools"))
import lift                                                     # noqa: E402
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_MEM_UNMAPPED  # noqa: E402
from unicorn import x86_const as X                              # noqa: E402

B = root / "build/difftest"
IMG_LO, DATA_LO, IMG_HI = 0x400000, 0x8CC000, 0x1465000
SCR_LO, SCR_SZ, STK_LO, STK_SZ = 0x20000000, 0x40000, 0x10000000, 0x10000
OUT_VA, RET_VA = 0x2FFF0000, 0x7FFE0000
# fp_ld80/fp_st80: the model's x87 is 53-bit (runtime/DESIGN.md); an 80-bit load of random data
# keeps 64 mantissa bits on hardware, so those bodies would differ by design, not by bug.
# `ret imm16` with an absurd pop only occurs in junk and trips a Unicorn quirk (16-bit wrap).
SKIP_RET = re.compile(r"esp \+= (\d+);(?: \{ RECOMP_REGS_OUT\(\); return; \} /\* 0x[0-9A-F]+: ret| if \(_ra != )")
RET_JUMP = re.compile(r"if \(_ra != RECOMP_RETADDR\) \{ (?:RECOMP_FLAGS_OUT\(\); )?RECOMP_ITAIL\(_ra\);")   # tools/lift.py ret_jumps
SKIP = re.compile(r"RECOMP_CALL|RECOMP_ICALL|RECOMP_ITAIL|recomp_fault|CPUID|__rdtsc|LIFT_ERROR|fp_ld80|fp_st80|"
                  r"\b(sin|cos|tan|atan2|atan|pow|log|log2|exp|exp2)\(")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sample", type=int, default=500)
    ap.add_argument("--seed", type=int, default=8)
    ap.add_argument("--dump", help="sub_XXXXXXXX: write both machines' final scratch+stack and diff them")
    ap.add_argument("--grep", help="only bodies whose lifted C matches this regex (e.g. '_mm\\[' for MMX)")
    a = ap.parse_args()
    want = re.compile(a.grep) if a.grep else None
    B.mkdir(parents=True, exist_ok=True)
    log = open(root / "logs/difftest.log", "w")
    lift._init()
    E = lift._G["entries"]
    rng = random.Random(a.seed)
    order = list(range(len(E)))
    rng.shuffle(order)
    chosen, bodies = [], []
    for idx in order:
        code, ok = lift.lift_one(idx)
        big_ret = any(int(n) > 0x404 for n in SKIP_RET.findall(code))
        if ok and not SKIP.search(RET_JUMP.sub("", code)) and not big_ret and code.count("\n") < 4000 and \
                (want is None or want.search(code)):
            chosen.append(idx); bodies.append(code)
        if len(chosen) >= a.sample * 2:
            break
    # ---- build the harness
    src = ['#define RECOMP_GENERATED_CODE', '#define RECOMP_LOCAL_REGS', '#include "recomp_types.h"', '']
    src += [f"void sub_{E[i]:08X}(void);" for i in chosen]
    src += bodies
    src.append("typedef void (*fn_t)(void);")
    src.append("const fn_t dt_funcs[] = {" + ", ".join(f"sub_{E[i]:08X}" for i in chosen) + "};")
    (B / "funcs.c").write_text("\n".join(src) + "\n")
    exe = B / "harness"
    cc = ["clang", "-O1", "-w", "-fwrapv", "-fno-strict-aliasing", "-DRECOMP_LOCAL_REGS", "-DRECOMP_RETADDR=0x7FFE0000u",   # = RET_VA: lifted rets tell returns from jumps by it
           "-I", str(root / "runtime/include/recomp"),
          "-I", str(root / "runtime/include"), str(root / "tools/difftest_harness.c"), str(B / "funcs.c"),
          str(root / "runtime/core/cpu_ext.c"), "-o", str(exe), "-lm"]
    r = subprocess.run(cc, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-4000:], file=log)
        print(f"difftest: FAIL harness build (see logs/difftest.log)")
        return 1
    # ---- memory images
    import pefile
    pe = pefile.PE(str(root / "Rugby08_unpacked.exe"), fast_load=True)
    img = bytearray(pe.get_memory_mapped_image()[:IMG_HI - IMG_LO])
    img += bytes(IMG_HI - IMG_LO - len(img))
    # The reference runs the same bytes the lifter decodes: the obfuscated calls rewritten
    # (tools/code_patch.py), equivalent on every real path.
    lo, hi = lift.ranges.CODE_LO - IMG_LO, lift.ranges.CODE_HI - IMG_LO
    img[lo:hi] = lift._G["code"]
    img = bytes(img)
    (B / "image.bin").write_bytes(img)
    # No NaN/Inf bit patterns in the scratch heap: NaN payload and sign are not part of the model
    # (x87, arm64 and wasm all propagate them differently; see runtime/DESIGN.md). Clearing bit 30
    # of any dword whose float exponent is all-ones also rules out double NaN/Inf high words.
    scr = bytearray(rng.getrandbits(8) for _ in range(SCR_SZ))
    # Doubles get game-like magnitudes (exponent 0x380..0x47F, ~1e-38..1e38): the x87 keeps a
    # 15-bit exponent even at double precision, so sums near 1e308 stay finite on hardware and
    # overflow in the model -- out of model by design (runtime/DESIGN.md).
    # Every 4-aligned dword can be the high half of a double (pointers into scratch are 4-aligned).
    for o in range(0, SCR_SZ, 4):
        hi = int.from_bytes(scr[o:o + 4], "little")
        e = (hi >> 20) & 0x7FF
        hi = (hi & ~(0x7FF << 20)) | ((0x380 + (e & 0xFF)) << 20)
        scr[o:o + 4] = hi.to_bytes(4, "little")
    for o in range(0, SCR_SZ, 4):
        if (scr[o + 3] & 0x7F) == 0x7F and (scr[o + 2] & 0x80):
            scr[o + 3] &= 0xBF
    scr = bytes(scr)
    (B / "scratch.bin").write_bytes(scr)

    # ---- reference runs
    cases, ref = [], {}
    excluded = {}
    for k, idx in enumerate(chosen):
        regs = [SCR_LO + (rng.randrange(0, SCR_SZ - 0x1000) & ~3) for _ in range(8)]
        regs[4] = STK_LO + 0xF000                  # esp
        regs[5] = STK_LO + 0xFF00                  # ebp: a frame above the args
        args = []
        for _ in range(8):
            c = rng.random()
            args.append(SCR_LO + (rng.randrange(0, SCR_SZ - 0x1000) & ~3) if c < 0.5 else
                        rng.randrange(0, 256) if c < 0.8 else rng.getrandbits(32))
        fail = None
        for pre in (0, 2):
            mu = Uc(UC_ARCH_X86, UC_MODE_32)
            mu.mem_map(IMG_LO, IMG_HI - IMG_LO); mu.mem_write(IMG_LO, img)
            mu.mem_map(SCR_LO, SCR_SZ); mu.mem_write(SCR_LO, scr)
            mu.mem_map(STK_LO, STK_SZ)
            mu.mem_map(OUT_VA, 0x1000)
            mu.mem_map(RET_VA, 0x1000); mu.mem_write(RET_VA, bytes([0xDD, 0x15]) + OUT_VA.to_bytes(4, "little"))
            pages = []
            def unmapped(uc, access, addr, size, value, ud):
                for pg in {addr & ~0xFFF, (addr + max(size, 1) - 1) & ~0xFFF}:
                    if pg not in pages and len(pages) < 64 and pg not in (0,):
                        try:
                            uc.mem_map(pg, 0x1000); pages.append(pg)
                        except UcError:
                            return False
                return True
            mu.hook_add(UC_HOOK_MEM_UNMAPPED, unmapped)
            esp = regs[4]
            for v in reversed(args):
                esp -= 4; mu.mem_write(esp, v.to_bytes(4, "little"))
            esp -= 4; mu.mem_write(esp, RET_VA.to_bytes(4, "little"))
            for rid, v in zip((X.UC_X86_REG_EAX, X.UC_X86_REG_ECX, X.UC_X86_REG_EDX, X.UC_X86_REG_EBX,
                               X.UC_X86_REG_ESP, X.UC_X86_REG_EBP, X.UC_X86_REG_ESI, X.UC_X86_REG_EDI),
                              regs[:4] + [esp] + regs[5:]):
                mu.reg_write(rid, v)
            mu.reg_write(X.UC_X86_REG_EFLAGS, 0x202)
            mu.reg_write(X.UC_X86_REG_FPCW, 0x027F)
            # Same values on the x87 stack in both machines: st1 = 1.5, st0 = -2.25 (functions like
            # the CRT's _CIsin take their argument there), used only when an empty-stack run underflows.
            mu.mem_write(OUT_VA + 0x10, struct.pack("<dd", 1.5, -2.25))
            mu.mem_write(RET_VA + 0x100, bytes([0xDD, 0x05]) + (OUT_VA + 0x10).to_bytes(4, "little") +
                         bytes([0xDD, 0x05]) + (OUT_VA + 0x18).to_bytes(4, "little"))
            try:
                if pre:
                    mu.emu_start(RET_VA + 0x100, RET_VA + 0x10C)
                mu.emu_start(E[idx], RET_VA, count=3_000_000)
            except UcError as e:
                fail = str(e); break
            if mu.reg_read(X.UC_X86_REG_EIP) != RET_VA:
                fail = "runaway"; break
            # An empty-stack run that ends at depth 2..7 underflowed: the function takes its input
            # on the x87 stack, so rerun it with values there (pre=2).
            if pre == 0 and ((8 - ((mu.reg_read(X.UC_X86_REG_FPSW) >> 11) & 7)) & 7) not in (0, 1):
                fail = "x87 underflow"; continue
            fail = None; break
        # x87 exception flags (status word bits 0-5) are not modeled: only a problem when this body
        # reads the status word AND the reference actually raised one (e.g. ZE after x/0).
        if not fail and (mu.reg_read(X.UC_X86_REG_FPSW) & 0x3F) and re.search(r"fn?stsw", bodies[k]):
            fail = "x87 exception flags read"
        if fail:
            excluded[fail] = excluded.get(fail, 0) + 1
            continue
        depth = (8 - ((mu.reg_read(X.UC_X86_REG_FPSW) >> 11) & 7)) & 7
        if depth:
            try:
                mu.emu_start(RET_VA, RET_VA + 6)
            except UcError:
                pass
        rv = [mu.reg_read(r) for r in (X.UC_X86_REG_EAX, X.UC_X86_REG_ECX, X.UC_X86_REG_EDX,
                                         X.UC_X86_REG_EBX, X.UC_X86_REG_ESP, X.UC_X86_REG_EBP,
                                         X.UC_X86_REG_ESI, X.UC_X86_REG_EDI)]
        hx = 1469598103934665603
        for i, pg in enumerate(pages):
            hx ^= (fnv_c(mu.mem_read(pg, 0x1000)) + i) & 0xFFFFFFFFFFFFFFFF
        hashes = {"data": fnv_c(mu.mem_read(DATA_LO, IMG_HI - DATA_LO)), "scr": fnv_c(mu.mem_read(SCR_LO, SCR_SZ)),
                  "stk": fnv_c(mu.mem_read(STK_LO, STK_SZ)), "out": fnv_c(mu.mem_read(OUT_VA, 8)), "pages": hx}
        ref[k] = (rv, depth, hashes)
        if a.dump == f"sub_{E[idx]:08X}":
            (B / "dump_uc.bin").write_bytes(bytes(mu.mem_read(SCR_LO, SCR_SZ)) + bytes(mu.mem_read(STK_LO, STK_SZ)))
            os.environ["DT_DUMP_IDX"] = str(k)
        cases.append(f"{k} " + " ".join(f"{v:08X}" for v in regs) + " " +
                     " ".join(f"{v:08X}" for v in args) + f" {pre} {len(pages)} " +
                     " ".join(f"{p:08X}" for p in pages))
    (B / "cases.txt").write_text("\n".join(cases) + "\n")
    r = subprocess.run([str(exe), str(B / "image.bin"), str(B / "scratch.bin"), str(B / "cases.txt")],
                       capture_output=True, text=True, timeout=3600)
    if a.dump and (B / "dump_c.bin").exists() and (B / "dump_uc.bin").exists():
        c, u = (B / "dump_c.bin").read_bytes(), (B / "dump_uc.bin").read_bytes()
        for o in range(0, len(c), 4):
            if c[o:o + 4] != u[o:o + 4]:
                va = (SCR_LO + o) if o < SCR_SZ else (STK_LO + o - SCR_SZ)
                gc, gu = struct.unpack("<I", c[o:o + 4])[0], struct.unpack("<I", u[o:o + 4])[0]
                print(f"dump {va:08X}: lifted {gc:08X} ({struct.unpack('<f', c[o:o+4])[0]:.9g})  "
                      f"ref {gu:08X} ({struct.unpack('<f', u[o:o+4])[0]:.9g})")
    compared = mism = 0
    fails = []
    for line in r.stdout.splitlines():
        f = line.split()
        k, st = int(f[0]), int(f[1])
        rv, depth, h = ref[k]
        name = f"sub_{E[chosen[k]]:08X}"
        if st == 5:     # computed jump out of the body (difftest_harness.c recomp_miss)
            excluded["computed jump"] = excluded.get("computed jump", 0) + 1; continue
        if st != 0:
            fails.append(f"{name}: lifted status {st} (1=signal 2=timeout 3=fault) but reference returned")
            mism += 1; compared += 1; continue
        got = [int(x, 16) for x in f[2:10]]
        diffs = [n for n, g, w in zip("eax ecx edx ebx esp ebp esi edi".split(), got, rv) if g != w]
        if int(f[10]) != depth:
            diffs.append(f"fpdepth {f[10]}!={depth}")
        if int(f[11], 16) != h["data"]: diffs.append(".data")
        if int(f[12], 16) != h["scr"]: diffs.append("scratch")
        if int(f[13], 16) != h["stk"]: diffs.append("stack")
        if int(f[14], 16) != h["pages"]: diffs.append("pages")
        if depth and int(f[15], 16) != h["out"]: diffs.append("st0")
        compared += 1
        if diffs:
            mism += 1
            fails.append(f"{name}: " + " ".join(diffs) + f"  got {' '.join(f[2:10])} want {' '.join(f'{v:08X}' for v in rv)}")
    for x in fails:
        print(x, file=log)
    print(json.dumps({"compared": compared, "mismatches": mism, "excluded": excluded,
                      "candidates": len(chosen)}), file=log)
    ok = compared >= a.sample and mism == 0
    print(f"difftest: compared={compared} mismatches={mism} excluded={sum(excluded.values())} -> {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1

_FNV_LIB = None
def canon(b):
    """NaN canonicalisation, identical to the harness's canon(): doubles, then floats."""
    import numpy as np
    a = bytearray(b)
    n8 = len(a) // 8 * 8
    q = np.frombuffer(a, dtype=np.uint64, count=n8 // 8)
    q[((q >> np.uint64(52)) & np.uint64(0x7FF) == np.uint64(0x7FF)) & ((q & np.uint64(0xFFFFFFFFFFFFF)) != 0)] = np.uint64(0x7FF8000000000000)
    d = np.frombuffer(a, dtype=np.uint32, count=len(a) // 4)
    d[((d >> np.uint32(23)) & np.uint32(0xFF) == np.uint32(0xFF)) & ((d & np.uint32(0x7FFFFF)) != 0)] = np.uint32(0x7FC00000)
    return bytes(a)

def fnv_c(b):
    """FNV-1a 64 of bytes, via numpy-free pure python for small, chunked C-speed for big."""
    global _FNV_LIB
    if _FNV_LIB is None:
        import ctypes
        so = B / "fnv.dylib"
        if not so.exists():
            (B / "fnv.c").write_text("#include <stdint.h>\n#include <stddef.h>\nuint64_t fnv(const uint8_t*p,size_t n){"
                                     "uint64_t h=1469598103934665603ull;for(size_t i=0;i<n;i++){h^=p[i];h*=1099511628211ull;}return h;}\n")
            subprocess.run(["clang", "-O2", "-shared", "-o", str(so), str(B / "fnv.c")], check=True)
        _FNV_LIB = ctypes.CDLL(str(so))
        _FNV_LIB.fnv.restype = ctypes.c_uint64
        _FNV_LIB.fnv.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    b = canon(b)
    return _FNV_LIB.fnv(b, len(b))

if __name__ == "__main__":
    sys.exit(main())
