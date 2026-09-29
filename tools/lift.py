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
import generate, lift32, lift_ext, code_patch             # noqa: E402
from pe_analyze import analyze_pe, build_iat_map          # noqa: E402
from capstone import Cs, CS_ARCH_X86, CS_MODE_32          # noqa: E402

EXE = str(root / "Rugby08_unpacked.exe")
OUT = root / "gen/recomp"
SPLIT = 400
_G = {}

def _init():
    info = analyze_pe(EXE)
    pe_data = open(EXE, "rb").read()
    text = [s for s in info.sections if s.name == ".text"][0]
    raw = pe_data[text.raw_offset:text.raw_offset + min(text.virtual_size, text.raw_size)]
    _G["code"], _G["obf_sites"] = code_patch.patch(raw, info.code_start, info.code_start + len(raw))
    _G["code_start"] = info.code_start
    _G["code_end"] = info.code_end
    _G["iat"] = build_iat_map(info)
    cat = json.load(open(root / "analysis/lift_catalog.json"))["entries"]
    _G["entries"] = cat
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    _G["md"] = md
    lifter = lift_ext.Lifter(iat_map=_G["iat"])
    lifter.lifted = set(cat)     # calls to anything else go through RECOMP_ICALL (logged miss)
    _G["lifter"] = lifter

def lift_one(idx):
    """Lift catalog entry idx -> (C code, ok). Shared by main() and tools/difftest.py."""
    E, code, cs, ce = _G["entries"], _G["code"], _G["code_start"], _G["code_end"]
    addr = E[idx]
    end = min(E[idx + 1] if idx + 1 < len(E) else ce, addr + 0x10000)
    name = f"sub_{addr:08X}"
    try:
        insns, leaders = generate.linear_disassemble_function(_G["md"], code, cs, addr, end)
        trimmed, seen_ret = [], False
        for i in insns:
            if i.mnemonic == "int3":
                break
            if seen_ret:
                if i.address not in leaders:
                    continue
                seen_ret = False
            trimmed.append(i)
            if i.is_ret:
                seen_ret = True
        if not trimmed:
            return f"void {name}(void) {{ recomp_fault(0x{addr:08X}u, \"#UD empty entry\"); }}\n", False
        return generate.lift_function_linear(_G["lifter"], name, trimmed, leaders, addr), True
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
