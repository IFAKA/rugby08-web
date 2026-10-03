#!/usr/bin/env python3
"""Read-only lift fidelity report, independent of features.json checks.

Compares the lifted C in gen/recomp against two references: Ghidra's function
extents (analysis/ghidra_funcs.csv) and a capstone linear decode of those
extents from the unpacked exe. Prints ONE summary line; details go to
logs/fidelity_report.log and logs/fidelity_report.json. Changes nothing else.
"""
import bisect, collections, glob, json, os, re
import capstone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
pe = json.load(open("analysis/pe_analysis.json"))
BASE = pe["image_base"]
exe = open(pe["filepath"], "rb").read()
cat = json.load(open("analysis/lift_catalog.json"))
entries = set(cat["entries"])
(t_lo, t_hi), other = cat["code_ranges"][0], cat["code_ranges"][1:]

ref = []  # Ghidra extents inside .text
for line in open("analysis/ghidra_funcs.csv"):
    s, e = (int(x, 16) for x in line.strip().split(","))
    if t_lo <= s < t_hi:
        ref.append((s, e))
ref.sort()
starts = [s for s, _ in ref]
ref_start = set(starts)

def owner(va):
    i = bisect.bisect_right(starts, va) - 1
    if i >= 0 and va < ref[i][1]:
        return ref[i]
    return None

# reference instruction boundaries: linear decode of each Ghidra extent
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
bounds = set()
for s, e in ref:
    off = s - BASE
    for ins in md.disasm(exe[off:e - BASE], s):
        bounds.add(ins.address)

# lifted instruction addresses and fall-through tails from gen/
fn_re = re.compile(r"^void sub_([0-9A-F]{8})\(void\)")
ins_re = re.compile(r"/\* 0x([0-9A-F]{8}): ")
tail_re = re.compile(r"RECOMP_ITAIL\(0x([0-9A-F]{8})u\)")
fn_insns = {}
tails = []  # (from_fn, target)
stubs = collections.Counter()
cur = None
for path in sorted(glob.glob("gen/recomp/recomp_*.c")):
    for line in open(path, errors="replace"):
        m = fn_re.match(line)
        if m:
            cur = int(m.group(1), 16)
            fn_insns[cur] = []
            if "recomp_fault(" in line:
                stubs[line.split('"')[1] if '"' in line else "fault"] += 1
            continue
        if cur is None:
            continue
        for a in ins_re.findall(line):
            fn_insns[cur].append(int(a, 16))
        for t in tail_re.findall(line):
            tails.append((cur, int(t, 16)))

defined = set(fn_insns)
in_text = lambda v: t_lo <= v < t_hi
cls = collections.Counter()
frag_examples = []
for v in entries:
    if not in_text(v):
        cls["outside_.text(protector/other)"] += 1
    elif v in ref_start:
        cls["at_ghidra_start"] += 1
    elif owner(v):
        cls["inside_ghidra_function(fragment)"] += 1
        if len(frag_examples) < 15:
            frag_examples.append(v)
    else:
        cls["in_.text_gap(no ghidra function)"] += 1
ghidra_unlifted = [s for s in starts if s not in entries]

# lifted instruction vs reference boundaries (only addresses inside Ghidra extents)
total_i = mis_i = 0
mis_by_fn = collections.Counter()
for fn, addrs in fn_insns.items():
    for a in addrs:
        if in_text(a) and owner(a):
            total_i += 1
            if a not in bounds:
                mis_i += 1
                mis_by_fn[fn] += 1
bad_fns = len(mis_by_fn)

# fall-through chain targets that are not lifted functions
tgt_missing = sorted({t for _, t in tails if t not in defined})
tgt_empty_stub = sorted({t for _, t in tails if t in defined and not fn_insns[t]})
tail_total = len(tails)

# byte coverage: Ghidra-function bytes whose reference instruction was lifted
lifted_addrs = {a for addrs in fn_insns.values() for a in addrs}
ref_bytes = ok_bytes = 0
for ins in (i for s, e in ref for i in md.disasm(exe[s - BASE:e - BASE], s)):
    ref_bytes += ins.size
    if ins.address in lifted_addrs:
        ok_bytes += ins.size

rep = {
    "entries": len(entries), "ghidra_functions": len(ref),
    "entry_classes": dict(cls), "ghidra_functions_without_lifted_entry": len(ghidra_unlifted),
    "lifted_insns_in_ghidra_extents": total_i,
    "lifted_insns_off_reference_boundary": mis_i, "functions_with_off_boundary_insns": bad_fns,
    "ref_bytes": ref_bytes, "ref_bytes_lifted_pct": round(100 * ok_bytes / ref_bytes, 2),
    "fallthrough_itail_sites": tail_total,
    "fallthrough_targets_not_lifted": len(tgt_missing),
    "fallthrough_targets_empty_stub": len(tgt_empty_stub),
    "fault_stubs": dict(stubs),
    "examples": {"fragment_entries": [hex(x) for x in frag_examples],
                 "fallthrough_targets_not_lifted": [hex(x) for x in tgt_missing[:20]],
                 "worst_off_boundary_fns": [(hex(f), n) for f, n in mis_by_fn.most_common(10)],
                 "ghidra_unlifted": [hex(x) for x in ghidra_unlifted[:20]]},
    "region_0x407590_4075A0": {
        "entries": [hex(v) for v in sorted(entries) if 0x407580 <= v <= 0x4075B0],
        "ghidra_extents": [(hex(s), hex(e)) for s, e in ref if s <= 0x4075A0 and e > 0x407580],
    },
}
os.makedirs("logs", exist_ok=True)
json.dump(rep, open("logs/fidelity_report.json", "w"), indent=1)
with open("logs/fidelity_report.log", "w") as f:
    json.dump(rep, f, indent=1)
print(f"fidelity: entries={len(entries)} fragment_entries={cls['inside_ghidra_function(fragment)']} "
      f"off_boundary_insns={mis_i}/{total_i} ref_bytes_lifted={rep['ref_bytes_lifted_pct']}% "
      f"fallthrough_targets_not_lifted={len(tgt_missing)} ghidra_unlifted={len(ghidra_unlifted)}")
