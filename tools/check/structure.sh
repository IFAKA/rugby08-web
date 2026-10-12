#!/usr/bin/env bash
# Project structure rules (docs/ARCHITECTURE.md). One summary line; details in logs/structure.log.
#   size     hand-written sources stay <= 300 lines (declaration headers and vendored code are exempt)
#   web      __EMSCRIPTEN__ only in platform/web/ and render/webgl/ (the rest of the code is target-neutral)
#   readme   every source directory has a README.md saying what lives there
#   vendor   third_party/ submodules sit on an upstream commit (our changes live in third_party/patches/)
#   tools/check/structure.sh [--report]   (--report: always exit 0)
cd "$(dirname "$0")/../.." || exit 1
mkdir -p logs
LOG=logs/structure.log
: >"$LOG"
MAX=300
# grep-only exemptions: API declaration headers and vendored files
EXEMPT='^(render/webgl/d3d9\.h|runtime/include/recomp/recomp_types\.h|runtime/third_party/|platform/native/glshim/)'

size=0
echo "==== size (> $MAX lines)" >>"$LOG"
while read -r f; do
  n=$(wc -l <"$f")
  if [ "$n" -gt "$MAX" ]; then
    echo "$n $f" >>"$LOG"
    size=$((size + 1))
  fi
done < <(git ls-files 'runtime/*' 'platform/*' 'render/*' 'game/*' 'tools/*' 'scripts/*' |
  grep -E '\.(c|cpp|h|hpp|js|py|sh|html|glsl|vert|frag)$' | grep -vE "$EXEMPT")

echo "==== web (__EMSCRIPTEN__ outside platform/web, render/webgl)" >>"$LOG"
web=$(git grep -c '__EMSCRIPTEN__' -- 'runtime/*' 'platform/*' 'render/*' 'game/*' |
  grep -vE '^(platform/web/|render/webgl/|runtime/third_party/)' | tee -a "$LOG" | grep -c .)

echo "==== readme (source directories without README.md)" >>"$LOG"
readme=0
for d in $(git ls-files 'runtime/*' 'platform/*' 'render/*' 'game/*' 'tools/*' | xargs -n1 dirname | sort -u |
  grep -vE '^(runtime/third_party|platform/native/glshim)'); do
  if [ ! -f "$d/README.md" ]; then
    echo "$d" >>"$LOG"
    readme=$((readme + 1))
  fi
done

echo "==== vendor (submodules with our own commits or edits)" >>"$LOG"
vendor=0
for s in $(git config -f .gitmodules --get-regexp 'submodule\..*\.path' | awk '{print $2}'); do
  [ -e "$s/.git" ] || continue
  ours=$(git -C "$s" log --oneline --author="$(git config user.email)" HEAD | wc -l | tr -d ' ')
  if [ "$ours" != 0 ] || [ -n "$(git -C "$s" status --porcelain)" ]; then
    echo "$s ($ours commits of ours inside the submodule)" >>"$LOG"
    vendor=$((vendor + 1))
  fi
done

total=$((size + web + readme + vendor))
msg="files>${MAX}: $size, web code outside platform/web: $web, dirs without README: $readme, modified submodules: $vendor"
if [ "$total" = 0 ]; then
  echo "structure: ok"
  exit 0
fi
echo "structure: $total ($msg; see $LOG)"
[ "${1:-}" = --report ] && exit 0
exit 1
