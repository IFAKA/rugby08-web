#!/usr/bin/env bash
# Count instructions the lifter could not translate in gen/recomp (UNIMPLEMENTED / not yet
# implemented / LIFT_ERROR / unresolved). Usage: tools/lift/lift_report.sh [--max-unsupported N]
# One summary line; per-mnemonic breakdown -> logs/lift_report.log.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
max=-1
[ "${1:-}" = "--max-unsupported" ] && max="${2:-0}"
[ -f gen/recomp/recomp_dispatch.c ] || {
  echo "lift_report: no gen/recomp (run tools/lift/lift.py)"
  exit 1
}
{
  echo "== UNIMPLEMENTED by mnemonic"
  rg -o --no-filename 'UNIMPLEMENTED: [a-z0-9]+' gen/recomp | sort | uniq -c | sort -rn
  echo "== not yet implemented"
  rg -o --no-filename '/\* SSE [a-z0-9]+ not yet implemented' gen/recomp | sort | uniq -c | sort -rn
  echo "== LIFT_ERROR"
  rg --no-filename 'LIFT_ERROR' gen/recomp | head -50
  echo "== unresolved / indirect jcc"
  rg -c --no-filename '/\* unresolved \*/|indirect jcc' gen/recomp | paste -sd+ - | bc
} >logs/lift_report.log 2>&1
n=$(rg -c --no-filename 'UNIMPLEMENTED:|not yet implemented|LIFT_ERROR' gen/recomp | paste -sd+ - | bc)
n=${n:-0}
top=$(sed -n '2,4p' logs/lift_report.log | tr -s ' ' | tr '\n' ';')
echo "lift_report: unsupported=$n max=$max top:$top"
[ "$max" -lt 0 ] || [ "$n" -le "$max" ]
