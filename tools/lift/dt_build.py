"""tools/lift/difftest.py, part 1: the constants, the candidate bodies, the harness build and the memory images."""

import pathlib
import re
import subprocess
import sys

root = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(root / "tools/lift"))
import lift  # noqa: E402

B = root / "build/difftest"
IMG_LO, DATA_LO, IMG_HI = 0x400000, 0x8CC000, 0x1465000
SCR_LO, SCR_SZ, STK_LO, STK_SZ = 0x20000000, 0x40000, 0x10000000, 0x10000
OUT_VA, RET_VA = 0x2FFF0000, 0x7FFE0000
# fp_ld80/fp_st80: the model's x87 is 53-bit (runtime/DESIGN.md); an 80-bit load of random data
# keeps 64 mantissa bits on hardware, so those bodies would differ by design, not by bug.
# `ret imm16` with an absurd pop only occurs in junk and trips a Unicorn quirk (16-bit wrap).
SKIP_RET = re.compile(r"esp \+= (\d+);(?: \{ RECOMP_REGS_OUT\(\); return; \} /\* 0x[0-9A-F]+: ret| if \(_ra != )")
RET_JUMP = re.compile(
    r"if \(_ra != RECOMP_RETADDR\) \{ (?:RECOMP_FLAGS_OUT\(\); )?RECOMP_ITAIL\(_ra\);"
)  # tools/lift/lift.py ret_jumps
SKIP = re.compile(
    r"RECOMP_CALL|RECOMP_ICALL|RECOMP_ITAIL|recomp_fault|CPUID|__rdtsc|LIFT_ERROR|fp_ld80|fp_st80|"
    r"\b(sin|cos|tan|atan2|atan|pow|log|log2|exp|exp2)\("
)


def choose(a, rng):
    """Leaf bodies to test, in a seeded random order: (catalog indexes, lifted C)."""
    want = re.compile(a.grep) if a.grep else None
    E = lift._G["entries"]
    order = list(range(len(E)))
    rng.shuffle(order)
    chosen, bodies = [], []
    for idx in order:
        code, ok = lift.lift_one(idx)
        big_ret = any(int(n) > 0x404 for n in SKIP_RET.findall(code))
        if (
            ok
            and not SKIP.search(RET_JUMP.sub("", code))
            and not big_ret
            and code.count("\n") < 4000
            and (want is None or want.search(code))
        ):
            chosen.append(idx)
            bodies.append(code)
        if len(chosen) >= a.sample * 2:
            break
    return chosen, bodies


def build_harness(a, E, chosen, bodies, log):
    """funcs.c (the chosen bodies) + difftest_harness.c -> build/difftest/harness, or None."""
    src = ["#define RECOMP_GENERATED_CODE", "#define RECOMP_LOCAL_REGS", '#include "recomp_types.h"', ""]
    src += [f"void sub_{E[i]:08X}(void);" for i in chosen]
    src += bodies
    src.append("typedef void (*fn_t)(void);")
    src.append("const fn_t dt_funcs[] = {" + ", ".join(f"sub_{E[i]:08X}" for i in chosen) + "};")
    if a.interp:
        src.append("const uint32_t dt_vas[] = {" + ", ".join(f"0x{E[i]:08X}u" for i in chosen) + "};")
        src.append(f"const unsigned dt_n = {len(chosen)};")
    (B / "funcs.c").write_text("\n".join(src) + "\n")
    exe = B / "harness"
    cc = [
        "clang",
        "-O1",
        "-w",
        "-fwrapv",
        "-fno-strict-aliasing",
        "-DRECOMP_LOCAL_REGS",
        "-DRECOMP_RETADDR=0x7FFE0000u",  # = RET_VA: lifted rets tell returns from jumps by it
        "-I",
        str(root / "runtime/include/recomp"),
        "-I",
        str(root / "runtime/include"),
        str(root / "tools/lift/difftest_harness.c"),
        str(B / "funcs.c"),
        str(root / "runtime/core/cpu_ext.c"),
        "-o",
        str(exe),
        "-lm",
    ]
    if a.interp:
        uc = next((root / ".venv/lib").glob("python3*/site-packages/unicorn"))
        cc[1:1] = ["-DDT_INTERP", "-DRT_INTERP", "-I", str(uc / "include")]
        cc += [str(root / "runtime/core/interp.c"), str(uc / "lib/libunicorn.a"), "-lpthread"]
    r = subprocess.run(cc, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[-4000:], file=log)
        print("difftest: FAIL harness build (see logs/difftest.log)")
        return None
    return exe


def memory_images(rng):
    """image.bin (the exe with the obfuscated calls rewritten, as the lifter decodes it) and scratch.bin."""
    import pefile

    pe = pefile.PE(str(root / "Rugby08_unpacked.exe"), fast_load=True)
    img = bytearray(pe.get_memory_mapped_image()[: IMG_HI - IMG_LO])
    img += bytes(IMG_HI - IMG_LO - len(img))
    # The reference runs the same bytes the lifter decodes: the obfuscated calls rewritten
    # (tools/lift/code_patch.py), equivalent on every real path.
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
        hi = int.from_bytes(scr[o : o + 4], "little")
        e = (hi >> 20) & 0x7FF
        hi = (hi & ~(0x7FF << 20)) | ((0x380 + (e & 0xFF)) << 20)
        scr[o : o + 4] = hi.to_bytes(4, "little")
    for o in range(0, SCR_SZ, 4):
        if (scr[o + 3] & 0x7F) == 0x7F and (scr[o + 2] & 0x80):
            scr[o + 3] &= 0xBF
    scr = bytes(scr)
    (B / "scratch.bin").write_bytes(scr)

    return img, scr
