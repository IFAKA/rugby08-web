#!/usr/bin/env bash
# Lint + format check for every hand-written source (never gen/, third_party/, vendored files).
# One summary line; per-tool details in logs/lint.log.
#   tools/check/lint.sh            fail on any finding
#   tools/check/lint.sh --report   always exit 0 (baseline while the cleanup is in progress)
#   tools/check/lint.sh --fix      apply the formatters first (clang-format, ruff format, shfmt, prettier)
cd "$(dirname "$0")/../.." || exit 1
mkdir -p logs
LOG=logs/lint.log
: >"$LOG"
MODE=${1:-}
V=.venv/bin
N=node_modules/.bin

c_files() {
  git ls-files 'runtime/*.c' 'runtime/*.cpp' 'runtime/*.h' 'runtime/*.hpp' 'platform/*.c' 'platform/*.h' \
    'render/*.cpp' 'render/*.h' 'game/*.c' 'game/*.h' |
    grep -v -e '/third_party/' -e 'recomp/recomp_types.h' -e '/glshim/'
}
py_files() { git ls-files 'tools/*.py'; }
sh_files() { git ls-files '*.sh' | grep -v '^third_party/'; }
js_files() { git ls-files 'platform/web/*.js'; }

if [ "$MODE" = --fix ]; then
  c_files | xargs "$V/clang-format" -i
  py_files | xargs "$V/ruff" format -q
  py_files | xargs "$V/ruff" check -q --fix-only
  sh_files | xargs shfmt -w -i 2 -ci
  js_files | xargs "$N/prettier" --log-level warn --write
fi

section() { echo "==== $1" >>"$LOG"; }

# formatters: number of files that would change
section clang-format
fmt_c=0
for f in $(c_files); do
  "$V/clang-format" --dry-run --Werror "$f" >/dev/null 2>&1 || {
    echo "$f" >>"$LOG"
    fmt_c=$((fmt_c + 1))
  }
done
section ruff-format
fmt_py=$(py_files | xargs "$V/ruff" format --check --output-format concise 2>&1 | tee -a "$LOG" | grep -c 'would be reformatted\|^Would reformat')
section prettier
fmt_js=$(js_files | xargs "$N/prettier" --list-different 2>>"$LOG" | tee -a "$LOG" | grep -c .)

# linters: number of findings
section clang-tidy
if [ -f build/native/compile_commands.json ]; then
  tidy=$(c_files | grep -E '\.(c|cpp)$' | grep -v '^platform/web/' | xargs "$V/clang-tidy" -p build/native --quiet --extra-arg=-isysroot"$(xcrun --show-sdk-path)" 2>/dev/null |
    tee -a "$LOG" | grep -cE ': (warning|error): ')
else
  echo "skipped: no build/native/compile_commands.json" >>"$LOG"
  tidy=skip
fi
section ruff
ruff=$(py_files | xargs "$V/ruff" check --output-format concise 2>&1 | tee -a "$LOG" | grep -cE ':[0-9]+:[0-9]+: ')
section shellcheck
sc=$(sh_files | xargs shellcheck -S warning -f gcc 2>&1 | tee -a "$LOG" | grep -cE ':[0-9]+:[0-9]+: ')
section eslint
es=$(js_files | xargs "$N/eslint" -f json 2>>"$LOG" | tee -a "$LOG" | jq '[.[].messages | length] | add // 0')

total=$((fmt_c + fmt_py + fmt_js + ruff + sc + es))
[ "$tidy" != skip ] && total=$((total + tidy))
msg="unformatted files: c $fmt_c, py $fmt_py, js $fmt_js; findings: clang-tidy $tidy, ruff $ruff, shellcheck $sc, eslint $es"
if [ "$total" = 0 ]; then
  echo "lint: ok ($msg)"
  exit 0
fi
echo "lint: $total ($msg; see $LOG)"
[ "$MODE" = --report ] && exit 0
exit 1
