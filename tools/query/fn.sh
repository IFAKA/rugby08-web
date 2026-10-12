#!/usr/bin/env bash
# Print one lifted function from gen/recomp without reading the 100k-line files around it.
#   tools/query/fn.sh 0x0044A7B0          by address
#   tools/query/fn.sh CTackleDrill::vf1    by name (analysis/symbols.json, tools/analysis/symbols.py)
#   tools/query/fn.sh tackle               a name fragment lists the matches (case-insensitive)
#   --max N    print at most N lines of the body (default 200)
cd "$(dirname "$0")/../.." || exit 1
MAX=200
[ "${1:-}" = --max ] && {
  MAX=$2
  shift 2
}
q=${1:?usage: fn.sh ADDRESS|NAME}
SYM=analysis/symbols.json
if [[ $q =~ ^(0x)?[0-9A-Fa-f]{6,8}$ ]]; then
  va=$(printf '%08X' "0x${q#0x}")
else
  [ -f "$SYM" ] || {
    echo "fn: no $SYM (run tools/analysis/symbols.py)"
    exit 1
  }
  hits=$(jq -r --arg q "$q" 'to_entries[] | select(.value | ascii_downcase | contains($q | ascii_downcase)) | "\(.key) \(.value)"' "$SYM")
  n=$(printf '%s\n' "$hits" | grep -c .)
  [ "$n" = 0 ] && {
    echo "fn: no function named like '$q'"
    exit 1
  }
  exact=$(printf '%s\n' "$hits" | awk -v q="$q" '$2 == q')
  [ -n "$exact" ] && hits=$exact n=1
  if [ "$n" -gt 1 ]; then
    echo "fn: $n functions match '$q':"
    printf '%s\n' "$hits" | head -40
    exit 0
  fi
  va=$(printf '%s' "$hits" | awk '{print $1}' | sed 's/^0x//')
fi
name=$([ -f "$SYM" ] && jq -r --arg k "0x$va" '.[$k] // empty' "$SYM")
f=$(grep -l -m1 "^void sub_${va}(void)" gen/recomp/recomp_*.c 2>/dev/null | head -1)
[ -n "$f" ] || {
  echo "fn: sub_$va is not a lifted function entry (see analysis/lift_catalog.json)"
  exit 1
}
echo "// sub_$va${name:+ = $name}  ($f)"
# the body; an EH function is a static sub_X_eh body plus a sub_X wrapper: print both
awk -v pat="^(static )?void sub_${va}(_eh)?\\\\(void\\\\)" -v max="$MAX" '
  $0 ~ pat { on = 1; n = 0 }
  on { if (n < max) print; else if (n == max) print "    ... (--max " max " lines)"; n++ }
  on && /^}/ { on = 0; print "" }' "$f"
