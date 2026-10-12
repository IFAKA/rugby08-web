#!/usr/bin/env bash
# Single entry point. Every subcommand prints a short summary; details go to logs/.
#   ./init.sh status        one line per feature item (pass/fail) + next item
#   ./init.sh next          the next runnable item (deps satisfied, not passing)
#   ./init.sh check <id>    run the item's check; on exit 0 set passes=true
#   ./init.sh check-all     re-run every passing item's check (regression guard)
#   ./init.sh build-native | build-web | serve
#   ./init.sh lint [--report|--fix]   formatters + linters (tools/check/lint.sh)
#   ./init.sh structure [--report]    file size / boundary rules (tools/check/structure.sh)
set -uo pipefail
cd "$(dirname "$0")" || exit 1
mkdir -p logs
export PATH="$PWD/.venv/bin:/opt/homebrew/opt/openjdk@21/bin:/opt/homebrew/opt/ghidra/libexec/support:$PATH"
export JAVA_HOME=/opt/homebrew/opt/openjdk@21
F=features.json

next_item() {
  jq -r '.items as $all | [.items[] | select(.passes==false)
    | select(all(.deps[]; . as $d | ($all[] | select(.id==$d) | .passes)))][0].id // empty' "$F"
}

run_check() {
  local id="$1" cmd
  cmd=$(jq -r --arg id "$id" '.items[] | select(.id==$id) | .check' "$F")
  [ -n "$cmd" ] || {
    echo "check: unknown id $id"
    return 2
  }
  if bash -c "$cmd" >"logs/check_$id.log" 2>&1; then
    tmp=$(mktemp)
    jq --arg id "$id" '(.items[] | select(.id==$id) | .passes) = true' "$F" >"$tmp" && mv "$tmp" "$F"
    echo "check $id: PASS"
  else
    echo "check $id: FAIL (see logs/check_$id.log: $(tail -1 "logs/check_$id.log" | cut -c1-160))"
    return 1
  fi
}

case "${1:-status}" in
  status)
    jq -r '.items[] | "\(if .passes then "PASS" else "    " end)  \(.id)"' "$F"
    echo "passing: $(jq '[.items[]|select(.passes)]|length' "$F")/$(jq '.items|length' "$F")  next: $(next_item)"
    ;;
  next) next_item ;;
  check) run_check "${2:?id}" ;;
  check-all)
    fails=0
    for id in $(jq -r '.items[]|select(.passes)|.id' "$F"); do
      cmd=$(jq -r --arg id "$id" '.items[]|select(.id==$id)|.check' "$F")
      bash -c "$cmd" >"logs/check_$id.log" 2>&1 || {
        echo "REGRESSION $id"
        fails=1
      }
    done
    [ $fails = 0 ] && echo "check-all: no regressions"
    exit $fails
    ;;
  build-native) cmake -S . -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo >logs/native_cmake.log 2>&1 && cmake --build build/native -j >logs/native_build.log 2>&1 && echo "build-native: ok" || {
    echo "build-native: FAIL ($(grep -m1 -i error logs/native_build.log logs/native_cmake.log | cut -c1-200))"
    exit 1
  } ;;
  build-web)
    export EM_CACHE="$PWD/build/emcache"
    python3 -I tools/lift/split_big.py gen/recomp build/web-gen >logs/split_big.out 2>&1 || {
      echo "build-web: FAIL (tools/lift/split_big.py, logs/split_big.out)"
      exit 1
    }
    emcmake cmake -S . -B build/web-cmake -DCMAKE_BUILD_TYPE=Release >logs/web_cmake.log 2>&1 && cmake --build build/web-cmake -j >logs/web_build.log 2>&1 && echo "build-web: ok" || {
      echo "build-web: FAIL ($(grep -m1 -i error logs/web_build.log logs/web_cmake.log | cut -c1-200))"
      exit 1
    }
    ;;
  serve) exec python3 tools/web/serve.py build/web "${2:-8080}" ;;
  lint) exec tools/check/lint.sh "${2:-}" ;;
  structure) exec tools/check/structure.sh "${2:-}" ;;
  *)
    sed -n '2,10p' "$0"
    exit 2
    ;;
esac
