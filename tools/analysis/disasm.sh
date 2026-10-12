#!/usr/bin/env bash
# pcrecomp disasm32 recursive-descent function recovery -> analysis/functions.json.
# Extra args pass through (e.g. --seed-functions FILE). One summary line; details in logs/disasm32.log.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
mkdir -p analysis logs
.venv/bin/python third_party/pcrecomp/tools/disasm/disasm32.py Rugby08_unpacked.exe \
  --output analysis/functions.json "$@" >logs/disasm32.log 2>&1
rc=$?
echo "disasm: exit=$rc $(grep -E '^\[\*\] (Functions|Byte coverage)' logs/disasm32.log | tr '\n' ' ')"
exit $rc
