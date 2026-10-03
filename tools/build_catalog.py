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
sys.path.insert(0, str(root / "tools"))
import code_patch                                           # noqa: E402
from ranges import CODE_LO, CODE_HI, TEXT, PROT, INITTERM, in_code    # noqa: E402
T_LO, T_HI = TEXT
_pcode, OBF = code_patch.patch(img[CODE_LO - base:CODE_HI - base], CODE_LO, CODE_HI)
img = img[:CODE_LO - base] + _pcode + img[CODE_HI - base:]  # decode what the lifter decodes
md = Cs(CS_ARCH_X86, CS_MODE_32)
log = open(root / "logs/catalog.log", "w")

def in_text(a): return in_code(a)   # .text or the protector region (tools/ranges.py)

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
static = A / "static_seeds.json"            # tools/lift_fixpoint.sh: constant tail/call targets
if static.exists():
    st = {a for a in json.load(open(static)) if in_text(a)}
    src["static_new"] = len(st - entries); entries |= st
icall = A / "icall_seeds.json"
if icall.exists():
    ic = {int(x, 0) if isinstance(x, str) else x for x in json.load(open(icall))}
    ic = {a for a in ic if in_text(a)}
    src["icall_new"] = len(ic - entries); entries |= ic
# Pointers the program itself holds to code: every 4-aligned dword of .data that lands in .text
# (EH FuncInfo/unwind tables, __xc/__xi initializer tables, callback tables).
data = [s for s in pe.sections if s.Name.rstrip(b"\0") == b".data"][0]
D_LO = base + data.VirtualAddress
dbytes = img[D_LO - base:D_LO - base + data.Misc_VirtualSize]
datarefs = {int.from_bytes(dbytes[o:o + 4], "little") for o in range(0, len(dbytes) - 3, 4)}
datarefs = {v for v in datarefs if in_text(v)}

# disasm32 starts outside every Ghidra function are real code only when something points at
# them; decoding cleanly alone let data-decoded-as-code in, and its "call targets" then cut
# real functions apart (sub_00401000 split at 0x401007).
# Protector region (REVERSE.md): game functions reached via `jmp/call [ptr]` stubs whose pointer
# holds the target, and via direct call/jmp/jcc from .text (the counter triggers).
import struct as _st
gend_set = set(g_starts)
prot_seeds = set()
_mds = Cs(CS_ARCH_X86, CS_MODE_32)
_mds.skipdata = True        # a linear sweep of all .text: step over bytes that do not decode
for i in _mds.disasm(img[T_LO - base:T_HI - base], T_LO):
    if i.mnemonic in ("jmp", "call") and i.op_str.startswith("dword ptr [0x") and i.op_str.endswith("]"):
        try:
            p = int(i.op_str[len("dword ptr ["):-1], 16)
        except ValueError:
            continue
        if base <= p < base + len(img) - 4:
            v = _st.unpack("<I", img[p - base:p - base + 4])[0]
            if PROT[0] <= v < PROT[1]:
                prot_seeds.add(v)
    elif (i.mnemonic in ("call", "jmp") or i.mnemonic.startswith("j")) and i.op_str.startswith("0x"):
        v = int(i.op_str, 16)
        # only from real code: an instruction inside a Ghidra function (the sweep also decodes data)
        if PROT[0] <= v < PROT[1] and (inside_ghidra(i.address) or i.address in gend_set):
            prot_seeds.add(v)
prot_seeds = {v for v in prot_seeds if decodes_cleanly(v)}
src["protector_seeds_new"] = len(prot_seeds - entries); entries |= prot_seeds

# every _initterm slot (tools/ranges.py INITTERM): small initializers nothing else references
init = {_st.unpack("<I", img[a - base:a - base + 4])[0] for lo_, hi_ in INITTERM for a in range(lo_, hi_, 4)}
init = {v for v in init if in_code(v)}
src["initterm_new"] = len(init - entries); entries |= init

inv_ok = inv_bad = inv_noref = split = 0
for f in json.load(open(A / "functions.json"))["functions"]:
    a = f["address"]
    if a in entries or not in_text(a) or f.get("entry_kind", "start") != "start":
        continue
    if inside_ghidra(a):
        split += 1
    elif a not in datarefs:
        inv_noref += 1
    elif decodes_cleanly(a):
        entries.add(a); inv_ok += 1
    else:
        inv_bad += 1
src.update(invented_added=inv_ok, invented_rejected=inv_bad, invented_unreferenced=inv_noref, split_excluded=split)

# direct-call closure
rounds, added_total = 0, 0
while True:
    rounds += 1
    order = sorted(entries)
    new = set()
    for k, a in enumerate(order):
        end = min(order[k + 1] if k + 1 < len(order) else CODE_HI, a + 0x10000)
        if a < T_HI <= end:
            end = T_HI
        for i in md.disasm(img[a - base:end - base], a):
            if i.mnemonic == "call" and i.op_str.startswith("0x"):
                t = int(i.op_str, 16)
                if in_text(t) and t not in entries:
                    new.add(t)
            elif i.mnemonic.startswith("j") and i.op_str.startswith("0x") and not (a >= PROT[0]):
                t = int(i.op_str, 16)
                if PROT[0] <= t < PROT[1] and t not in entries and decodes_cleanly(t):
                    new.add(t)
            elif i.mnemonic in ("push", "mov") and ", 0x" in (", " + i.op_str):
                t = int(i.op_str.rsplit("0x", 1)[1], 16) if i.op_str.rsplit(" ", 1)[-1].startswith("0x") else 0
                if T_LO <= t < T_HI and t not in entries and not inside_ghidra(t) and decodes_cleanly(t):
                    new.add(t)
            if i.mnemonic in ("int3",):
                break
    if not new:
        break
    entries |= new; added_total += len(new)
    print(f"round {rounds}: +{len(new)} call targets", file=log)
src["call_targets_added"] = added_total

# A start inside a patched `push R; push T; ret` site (and its junk gap) is never a function: the
# lifter reads that span as `call T; jmp R`. Seeds (fixpoint fall-throughs, invented starts) that land
# there would cut the enclosing function short and leave a 5-byte call in a 3-byte "function".
_inside_site = {a for s, _, r in OBF for a in range(s, r)}
src["dropped_inside_obf_sites"] = len(entries & _inside_site)
entries -= _inside_site

out = sorted(entries)
json.dump({"code_ranges": [TEXT, PROT], "sources": src, "entries": out}, open(A / "lift_catalog.json", "w"))
print(json.dumps(src), file=log)
print(f"build_catalog: {len(out)} entries {src}")
