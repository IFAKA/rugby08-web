#!/usr/bin/env bash
# pcrecomp cpp/rtti.py: MSVC RTTI -> analysis/rtti.json (full), analysis/rtti_seeds.json (virtual
# method entries for disasm32 --seed-functions), analysis/vtables.json (one record per vtable).
# One summary line; details in logs/rtti.log.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
.venv/bin/python third_party/pcrecomp/tools/cpp/rtti.py Rugby08_unpacked.exe -o analysis/rtti.json \
  --seeds analysis/rtti_seeds.json --names analysis/rtti_names.json >logs/rtti.log 2>&1
rc=$?
[ $rc = 0 ] && jq '[.classes | to_entries[] | {class: .key} + .value]' analysis/rtti.json >analysis/vtables.json 2>>logs/rtti.log
echo "rtti: exit=$rc vtables=$(jq length analysis/vtables.json 2>/dev/null) seeds=$(jq length analysis/rtti_seeds.json 2>/dev/null) $(tail -1 logs/rtti.log | cut -c1-120)"
exit $rc
