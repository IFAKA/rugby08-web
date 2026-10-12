#!/usr/bin/env bash
# Behaviour regression for refactors: run fixed native scenarios (hidden, muted) and keep their
# screenshots, audio and log signature; compare a run against a baseline. One summary line;
# details in logs/regress.log.
#   tools/check/regress.sh capture DIR [--full]    record scenarios into DIR
#   tools/check/regress.sh compare BASE DIR         images/audio/log signature within tolerance
# Scenarios: intro (300 frames), menu (skip_to_menu replay, 4600 frames), audio (900 frames);
# --full adds match (tutorial_match replay to frame 17000, 14 shots of the 3D match).
cd "$(dirname "$0")/../.." || exit 1
mkdir -p logs
LOG=logs/regress.log
BIN=${REGRESS_BIN:-build/native/rugby08}
cmd=${1:?capture|compare}
shift

run() { # name, timeout, rugby08 args...
  local name=$1 tmo=$2
  shift 2
  SDL_AUDIODRIVER=dummy timeout "$tmo" "$BIN" --log "$OUT/$name.log" "$@" >"$OUT/$name.out" 2>&1
  echo "$name exit $?" >>"$OUT/status.txt"
  # signature: what must not change -- missing imports/methods, faults, dispatch misses
  grep -ohE 'UNIMPL [A-Za-z0-9_!:.() ]+|FAULT|FATAL|CPU fault|unresolved VA 0x[0-9A-F]+' "$OUT/$name.log" "$OUT/$name.out" |
    sort -u >"$OUT/$name.sig"
}

case "$cmd" in
  capture)
    OUT=${1:?DIR}
    full=${2:-}
    [ -x "$BIN" ] || { echo "regress: FAIL ($BIN missing)"; exit 1; }
    rm -rf "$OUT"
    mkdir -p "$OUT"
    : >"$OUT/status.txt"
    run intro 120 --frames 300 --screenshot "$OUT/intro.png"
    run menu 300 --frames 4600 --replay tests/replays/skip_to_menu.txt --screenshot "$OUT/menu.png"
    run audio 120 --frames 900 --audio-capture "$OUT/audio.wav"
    if [ "$full" = --full ]; then
      rm -rf logs/film
      mkdir -p logs/film
      run match 900 --frames 17000 --replay tests/replays/tutorial_match.txt
      mkdir -p "$OUT/match"
      cp logs/film/*.png "$OUT/match/" 2>/dev/null
    fi
    echo "regress: captured $(find "$OUT" -name '*.png' | wc -l | tr -d ' ') images into $OUT ($(tr '\n' ' ' <"$OUT/status.txt"))"
    ;;
  compare)
    BASE=${1:?BASE}
    OUT=${2:?DIR}
    .venv/bin/python -I tools/check/regress_cmp.py "$BASE" "$OUT" 2>&1 | tee "$LOG" | tail -1
    exit "${PIPESTATUS[0]}"
    ;;
  *)
    echo "regress: unknown command $cmd"
    exit 2
    ;;
esac
