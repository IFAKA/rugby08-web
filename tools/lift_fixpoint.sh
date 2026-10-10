#!/usr/bin/env bash
# Catalog <-> lift fixpoint: every constant RECOMP_ITAIL/RECOMP_ICALL target inside .text that is
# not a catalog entry becomes one (analysis/static_seeds.json), then catalog + lift rerun, until
# no new targets appear. One summary line; details in logs/lift_fixpoint.log.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
: > logs/lift_fixpoint.log

# start from no static seeds: seeds found by an older lift (e.g. before a code_patch rewrite) can
# land inside real instructions and cut bodies short ("#UD empty entry")
rm -f analysis/static_seeds.json
for round in $(seq 1 60); do   # 60: the SecuROM runtime range (tools/ranges.py) needs ~30 rounds
  .venv/bin/python tools/build_catalog.py >> logs/lift_fixpoint.log 2>&1 || { echo "lift_fixpoint: catalog failed"; exit 1; }
  .venv/bin/python tools/lift.py >> logs/lift_fixpoint.log 2>&1 || { echo "lift_fixpoint: lift failed"; exit 1; }
  new=$(.venv/bin/python - <<'PY'
import json, pathlib, re, sys
sys.path.insert(0, 'tools')
from ranges import in_code
import pefile
pe = pefile.PE("Rugby08_unpacked.exe", fast_load=True)
base, img = pe.OPTIONAL_HEADER.ImageBase, pe.get_memory_mapped_image()
cat = set(json.load(open("analysis/lift_catalog.json"))["entries"])
seeds_p = pathlib.Path("analysis/static_seeds.json")
seeds = set(json.load(open(seeds_p))) if seeds_p.exists() else set()
found = set()
for f in pathlib.Path("gen/recomp").glob("recomp_*.c"):
    for m in re.finditer(r"RECOMP_I(?:TAIL|CALL)\(0x([0-9A-F]{8})u\)", f.read_text()):
        v = int(m.group(1), 16)
        # `00 00` (add [eax], al) never starts real code: fall-through targets in zero fill would
        # otherwise walk the fill one byte per round and never converge
        if in_code(v) and v not in cat and img[v - base:v - base + 2] != b"\0\0":
            found.add(v)
new = found - seeds
json.dump(sorted(seeds | found), open(seeds_p, "w"))
print(len(new))
PY
)
  echo "round $round: $new new static targets" >> logs/lift_fixpoint.log
  if [ "$new" = 0 ]; then
    # code inside the AEMS sound banks (run from the heap) -> gen/aems/aems.c
    .venv/bin/python tools/aems_lift.py >> logs/lift_fixpoint.log 2>&1 || { echo "lift_fixpoint: aems_lift failed"; exit 1; }
    echo "lift_fixpoint: converged after $round rounds ($(jq length analysis/static_seeds.json) static seeds, $(jq '.entries|length' analysis/lift_catalog.json) entries, $(tail -1 logs/lift_fixpoint.log | sed 's/^aems_lift: //'))"; exit 0
  fi
done
echo "lift_fixpoint: not converged after 60 rounds"; exit 1
