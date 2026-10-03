#!/usr/bin/env python3
"""Lift every entry of analysis/lift_catalog.json to C in gen/recomp/ with pcrecomp's lifter.

Reuses pcrecomp tools/lift/generate.py (linear decode between catalog entries, trim after ret,
RECOMP_LOCAL_REGS-safe bodies, local switch dispatch) -- only the entry list is ours, because
generate.py finds entries heuristically and the translator re-runs discovery.
Parallel over chunks (multiprocessing). Prints one line; details -> logs/lift.log.
"""
import json, os, pathlib, sys, time
from multiprocessing import Pool

root = pathlib.Path(__file__).resolve().parent.parent
PC = root / "third_party/pcrecomp/tools"
for d in ("pe", "disasm", "lift"):
    sys.path.insert(0, str(PC / d))
sys.path.insert(0, str(root / "tools"))
import generate, lift32, lift_ext, code_patch, ranges, jumptables     # noqa: E402
from pe_analyze import analyze_pe, build_iat_map          # noqa: E402
from capstone import Cs, CS_ARCH_X86, CS_MODE_32          # noqa: E402

EXE = str(root / "Rugby08_unpacked.exe")
OUT = root / "gen/recomp"
SPLIT = 400
_G = {}

def _init():
    info = analyze_pe(EXE)
    pe_data = open(EXE, "rb").read()
    # One buffer over both code ranges (tools/ranges.py): .text and the protector region inside
    # .data. The dump is flat (raw == virtual), so image bytes are file bytes from ImageBase.
    import pefile
    img = pefile.PE(EXE, fast_load=True).get_memory_mapped_image()
    raw = img[ranges.CODE_LO - info.image_base:ranges.CODE_HI - info.image_base]
    _G["code"], _G["obf_sites"] = code_patch.patch(raw, ranges.CODE_LO, ranges.CODE_HI)
    _G["code_start"] = ranges.CODE_LO
    _G["code_end"] = ranges.CODE_HI
    _G["iat"] = build_iat_map(info)
    cat = json.load(open(root / "analysis/lift_catalog.json"))["entries"]
    _G["entries"] = cat
    _G["hooks"] = {int(k, 16): v for k, v in json.load(open(root / "tools/hooks.json")).items()}
    _G["end"] = body_ends(cat, ranges.CODE_HI)
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    _G["md"] = md
    lifter = lift_ext.Lifter(iat_map=_G["iat"])
    lifter.lifted = set(cat)     # calls to anything else go through RECOMP_ICALL (logged miss)
    _G["lifter"] = lifter

def body_ends(entries, code_end):
    """Where each entry's lifted body stops (see runtime/DESIGN.md "Function bodies").
    Ghidra functions are lifted over their whole extent -- decoding straight over any other entry
    inside them -- so loops stay local instead of becoming chains of tail calls. An entry inside a
    Ghidra function runs to that function's end; anything else stops at the next top-level entry."""
    import bisect, csv
    g = []
    for s, e in csv.reader(open(root / "analysis/ghidra_funcs.csv")):
        s, e = int(s, 16), int(e, 16)
        if 0 < e - s <= 0x40000:
            g.append((s, e))
    g.sort()
    gs = [s for s, _ in g]
    for i in range(len(g) - 1):             # contiguous MSVC bodies: never past the next function
        if g[i][1] > g[i + 1][0]:
            g[i] = (g[i][0], g[i + 1][0])
    gend = dict(g)
    def host(a):
        i = bisect.bisect_right(gs, a) - 1
        return g[i] if i >= 0 and g[i][0] <= a < g[i][1] else None
    top = sorted(a for a in entries if a in gend or host(a) is None)
    ends = {}
    for a in entries:
        h = host(a)
        if h:
            ends[a] = h[1]
        else:
            j = bisect.bisect_right(top, a)
            e = min(top[j] if j < len(top) else code_end, a + 0x10000)
            if a < ranges.TEXT[1] < e:          # never run from .text into .data
                e = ranges.TEXT[1]
            ends[a] = e
    return ends

def lift_one(idx):
    """Lift catalog entry idx -> (C code, ok). Shared by main() and tools/difftest.py."""
    E, code, cs, ce = _G["entries"], _G["code"], _G["code_start"], _G["code_end"]
    addr = E[idx]
    end = _G["end"][addr]
    name = f"sub_{addr:08X}"
    try:
        insns, leaders = jumptables.sweep(_G["md"], code, cs, addr, end)
        trimmed, seen_ret = [], False
        for i in insns:
            if i.mnemonic == "int3":
                break
            if seen_ret:
                if i.address not in leaders:
                    continue
                seen_ret = False
            trimmed.append(i)
            # after ret or an unconditional jmp (direct, or a switch's jmp [table]) the next bytes
            # are reachable only through a label: skip them until one (protector junk, tables)
            if i.is_ret or i.is_uncond_jump:
                seen_ret = True
        if not trimmed:
            return f"void {name}(void) {{ recomp_fault(0x{addr:08X}u, \"#UD empty entry\"); }}\n", False
        code = generate.lift_function_linear(_G["lifter"], name, trimmed, leaders, addr)
        hook = _G["hooks"].get(addr)
        if hook:   # tools/hooks.json: announce entry (checks grep for "<name> entered")
            code = code.replace(f"    RECOMP_ENTER(0x{addr:08X}u);",
                                f"    RECOMP_ENTER(0x{addr:08X}u);\n    rt_hook_enter(0x{addr:08X}u, \"{hook}\");", 1)
        return code, True
    except Exception as e:  # keep going; the report counts these
        return f"/* LIFT_ERROR {name}: {e} */\nvoid {name}(void) {{ }}\n", False

def _lift_chunk(job):
    file_idx, lo, hi = job
    funcs, errs = [], 0
    for idx in range(lo, hi):
        c, ok = lift_one(idx)
        funcs.append((c, _G["entries"][idx], f"sub_{_G['entries'][idx]:08X}"))
        errs += 0 if ok else 1
    generate.write_chunk(str(OUT), file_idx, funcs)
    return file_idx, hi - lo, errs

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for f in OUT.glob("recomp_*.c"):
        f.unlink()   # generated output only (gen/ is ours, gitignored)
    _init()
    E = _G["entries"]
    jobs = [(k, lo, min(lo + SPLIT, len(E))) for k, lo in enumerate(range(0, len(E), SPLIT))]
    t = time.time()
    with Pool(os.cpu_count(), initializer=_init) as p, open(root / "logs/lift.log", "w") as log:
        errs = 0
        for fi, n, e in p.imap_unordered(_lift_chunk, jobs):
            errs += e
            print(f"chunk {fi}: {n} funcs, {e} errors", file=log, flush=True)
    names = [f"sub_{a:08X}" for a in E]
    (OUT / "recomp_funcs.h").write_text("#pragma once\n#include <stdint.h>\n\n" +
        "".join(f"void {n}(void);\n" for n in names))
    (OUT / "recomp_dispatch.c").write_text('#include "recomp_types.h"\n#include "recomp_funcs.h"\n\n'
        "const recomp_dispatch_entry_t recomp_dispatch_table[] = {\n" +
        "".join(f"    {{ 0x{a:08X}u, sub_{a:08X} }},\n" for a in E) +
        f"}};\n\nconst uint32_t recomp_dispatch_count = {len(E)};\n")
    size = sum(f.stat().st_size for f in OUT.glob("*.c"))
    print(f"lift: {len(E)} functions in {len(jobs)} files, {errs} errors, {size/1048576:.0f} MB C, {time.time()-t:.0f}s")

if __name__ == "__main__":
    main()
