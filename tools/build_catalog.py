#!/usr/bin/env python3
"""Build the lift catalog (analysis/lift_catalog.json): the function entries to lift.

  seeds  = Ghidra starts (analysis/ghidra_funcs.csv, in .text)
         + RTTI virtual methods (analysis/rtti_seeds.json)
         + runtime indirect-call misses (analysis/icall_seeds.json, if present)
         + pcrecomp disasm32 starts that lie OUTSIDE every Ghidra function ("invented") and
           decode cleanly to a ret/jmp (real code Ghidra skipped; data decodes to junk fast)
  closure: linear-decode every entry up to the next entry; add every direct `call` target in
           .text; repeat to a fixpoint.
disasm32 starts that land INSIDE a Ghidra function ("split") are deliberately excluded: making
them entries would cut a real function into pieces. Prints one line; details -> logs/catalog.log.
"""
import bisect, csv, json, pathlib, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

root = pathlib.Path(__file__).resolve().parent.parent
A = root / "analysis"
pe = pefile.PE(str(root / "Rugby08_unpacked.exe"), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
text = [s for s in pe.sections if s.Name.rstrip(b"\0") == b".text"][0]
T_LO, T_HI = base + text.VirtualAddress, base + text.VirtualAddress + text.Misc_VirtualSize
sys.path.insert(0, str(root / "tools"))
import code_patch                                           # noqa: E402
_ptext, OBF = code_patch.patch(img[T_LO - base:T_HI - base], T_LO, T_HI)
img = img[:T_LO - base] + _ptext + img[T_HI - base:]      # decode what the lifter decodes
md = Cs(CS_ARCH_X86, CS_MODE_32)
log = open(root / "logs/catalog.log", "w")

def in_text(a): return T_LO <= a < T_HI

ghidra = []
for s, e in csv.reader(open(A / "ghidra_funcs.csv")):
    s, e = int(s, 16), int(e, 16)
    if in_text(s):
        ghidra.append((s, e))
ghidra.sort()
g_starts = [s for s, _ in ghidra]

def inside_ghidra(a):
    i = bisect.bisect_right(g_starts, a) - 1
    return i >= 0 and g_starts[i] < a < ghidra[i][1] and ghidra[i][1] - g_starts[i] < 0x40000

def decodes_cleanly(a, limit=4096):
    """True if linear decode from `a` reaches ret/jmp before any invalid byte or int3."""
    n = 0
    for i in md.disasm(img[a - base:a - base + limit], a):
        n += 1
        if i.mnemonic in ("int3", "hlt") or i.mnemonic.startswith("(bad"):
            return False
        if i.mnemonic in ("ret", "retn", "jmp"):
            return n >= 1
    return False

entries = set(g_starts)
src = {"ghidra": len(entries)}
rtti = {r["address"] for r in json.load(open(A / "rtti_seeds.json")) if in_text(r["address"])}
src["rtti_new"] = len(rtti - entries); entries |= rtti
obf = {tgt for _, tgt, _ in OBF}
src["obf_call_targets_new"] = len(obf - entries); entries |= obf
icall = A / "icall_seeds.json"
if icall.exists():
    ic = {int(x, 0) if isinstance(x, str) else x for x in json.load(open(icall))}
    ic = {a for a in ic if in_text(a)}
    src["icall_new"] = len(ic - entries); entries |= ic
inv_ok = inv_bad = split = 0
for f in json.load(open(A / "functions.json"))["functions"]:
    a = f["address"]
    if a in entries or not in_text(a) or f.get("entry_kind", "start") != "start":
        continue
    if inside_ghidra(a):
        split += 1
    elif decodes_cleanly(a):
        entries.add(a); inv_ok += 1
    else:
        inv_bad += 1
src.update(invented_added=inv_ok, invented_rejected=inv_bad, split_excluded=split)

# direct-call closure
rounds, added_total = 0, 0
while True:
    rounds += 1
    order = sorted(entries)
    new = set()
    for k, a in enumerate(order):
        end = min(order[k + 1] if k + 1 < len(order) else T_HI, a + 0x10000)
        for i in md.disasm(img[a - base:end - base], a):
            if i.mnemonic == "call" and i.op_str.startswith("0x"):
                t = int(i.op_str, 16)
                if in_text(t) and t not in entries:
                    new.add(t)
            if i.mnemonic in ("int3",):
                break
    if not new:
        break
    entries |= new; added_total += len(new)
    print(f"round {rounds}: +{len(new)} call targets", file=log)
src["call_targets_added"] = added_total

out = sorted(entries)
json.dump({"text": [T_LO, T_HI], "sources": src, "entries": out}, open(A / "lift_catalog.json", "w"))
print(json.dumps(src), file=log)
print(f"build_catalog: {len(out)} entries {src}")
