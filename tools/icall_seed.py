#!/usr/bin/env python3
"""Merge runtime dispatch misses (logs/icall_miss.txt from recomp_miss) into
analysis/icall_seeds.json -- targets inside .text become lift-catalog entries on the next
tools/lift_fixpoint.sh. Targets outside .text are reported, not seeded. Prints one line."""
import json, pathlib, re, sys
root = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / "tools"))
from ranges import in_code
seeds_p = root / "analysis/icall_seeds.json"
seeds = set(json.load(open(seeds_p))) if seeds_p.exists() else set()
new, outside = set(), []
miss = root / "logs/icall_miss.txt"
for line in (miss.read_text().splitlines() if miss.exists() else []):
    m = re.match(r"0x([0-9A-F]{8}) (ICALL|ITAIL) from 0x([0-9A-F]{8})", line)
    if not m:
        continue
    va = int(m.group(1), 16)
    if in_code(va):
        if va not in seeds:
            new.add(va)
    else:
        outside.append(line)
json.dump(sorted(seeds | new), open(seeds_p, "w"))
print(f"icall_seed: +{len(new)} seeds ({len(seeds | new)} total); {len(outside)} misses outside .text"
      + (f", e.g. {outside[0]}" if outside else ""))
