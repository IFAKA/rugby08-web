#!/usr/bin/env python3
"""Mnemonic census over Ghidra function bodies (analysis/ghidra_funcs.csv): how much SSE/MMX/
fs:/x87 the game uses. Writes analysis/insn_stats.json; prints one line."""
import csv, json, pathlib, collections, pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

root = pathlib.Path(__file__).resolve().parent.parent
pe = pefile.PE(str(root / "Rugby08_unpacked.exe"), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.skipdata = True
mn = collections.Counter()
xmm, mmx, fs, x87, total = collections.Counter(), collections.Counter(), 0, 0, 0
funcs_with = collections.Counter()
for row in csv.reader(open(root / "analysis/ghidra_funcs.csv")):
    s, e = int(row[0], 16), int(row[1], 16)
    if e - s > 0x20000 or s < base:
        continue
    seen = set()
    for i in md.disasm(img[s - base:e - base], s):
        total += 1
        mn[i.mnemonic] += 1
        o = i.op_str
        if "xmm" in o:
            xmm[i.mnemonic] += 1; seen.add("xmm")
        elif "mm" in o and "mmword" not in o:
            mmx[i.mnemonic] += 1; seen.add("mmx")
        if "fs:" in o:
            fs += 1; seen.add("fs")
        if i.mnemonic.startswith("f"):
            x87 += 1
    for k in seen:
        funcs_with[k] += 1
out = {"instructions": total, "x87": x87, "fs_refs": fs, "funcs_with": funcs_with,
       "xmm_top": xmm.most_common(40), "mmx_top": mmx.most_common(20), "mnemonics": len(mn),
       "rare": [m for m, c in mn.items() if c < 5]}
json.dump(out, open(root / "analysis/insn_stats.json", "w"), indent=1)
print(f"insn_stats: insns={total} x87={x87} xmm={sum(xmm.values())} mmx={sum(mmx.values())} "
      f"fs={fs} funcs_with={dict(funcs_with)} distinct_mnemonics={len(mn)}")
