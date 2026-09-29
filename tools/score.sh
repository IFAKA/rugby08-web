#!/usr/bin/env bash
# Score pcrecomp's catalog (analysis/functions.json) against Ghidra (analysis/ghidra_funcs.csv).
# Writes analysis/ghidra_funcs.json, analysis/score.json, analysis/ghidra_missing_seeds.json
# (Ghidra starts inside .text that disasm32 lacks; feed to `tools/disasm.sh --seed-functions`).
# One summary line; details in logs/score.log.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
.venv/bin/python - <<'EOF' > logs/score.log 2>&1
import json
text_lo, text_hi = 0x401000, 0x401000 + 0x4CB000
ref = []
for line in open("analysis/ghidra_funcs.csv"):
    s, e = line.strip().split(",")
    s, e = int(s, 16), int(e, 16)
    if text_lo <= s < text_hi:
        ref.append({"start": s, "end": e})
json.dump({"functions": ref}, open("analysis/ghidra_funcs.json", "w"))
cand = {f["address"] if isinstance(f["address"], int) else int(f["address"], 0)
        for f in json.load(open("analysis/functions.json"))["functions"]}
missing = sorted(r["start"] for r in ref if r["start"] not in cand)
json.dump(missing, open("analysis/ghidra_missing_seeds.json", "w"))
print(f"ghidra_in_text={len(ref)} candidate={len(cand)} missing_from_candidate={len(missing)}")
EOF
.venv/bin/python third_party/pcrecomp/tools/disasm/score_recovery.py \
  --reference analysis/ghidra_funcs.json --candidate analysis/functions.json \
  --range 0x401000-0x8CC000 -o analysis/score.json >> logs/score.log 2>&1
rc=$?
echo "score: exit=$rc $(head -1 logs/score.log) $(grep -i -m2 'recall\|precision' logs/score.log | tr '\n' ' ' | cut -c1-200)"
exit $rc
