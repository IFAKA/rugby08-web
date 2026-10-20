#!/bin/sh
# native.* checks: run build/native/rugby08 and require a trace line on its stdout. One summary line.
#   tools/check/run_native.sh [--frames N] [--timeout S] [--expect 'TEXT'] [extra rugby08 args...]
# Without --expect, the run must reach its last frame ('frame N').
# Game log: logs/native_run.log; stdout/stderr of the run: logs/run_native.log.
# Muted: SDL's dummy audio driver still runs the mixer callback in real time but plays nothing
# (set SDL_AUDIODRIVER to override; scripts/play.sh is the interactive run with sound).
cd "$(dirname "$0")/../.." || exit 1
FRAMES=0 TMO=${RUN_NATIVE_TIMEOUT:-120} EXPECT='' EXTRA=''
while [ $# -gt 0 ]; do
  case "$1" in
    --frames)
      FRAMES=$2
      shift 2
      ;;
    --timeout)
      TMO=$2
      shift 2
      ;;
    --expect)
      EXPECT=$2
      shift 2
      ;;
    --replay | --screenshot | --audio-capture)
      EXTRA="$EXTRA $1 $2"
      shift 2
      ;;
    *) break ;;
  esac
done
[ -n "$EXPECT" ] || [ "$FRAMES" -gt 0 ] || {
  echo "run_native: --expect or --frames required"
  exit 2
}
[ -n "$EXPECT" ] || EXPECT="frame $FRAMES"
[ -x build/native/rugby08 ] || {
  echo "run_native: FAIL (build/native/rugby08 missing; ./init.sh build-native)"
  exit 1
}
# RUN_NATIVE_MEMO=DIR (check_all.sh): a run with the same args and the same binary runs once; the
# repeats reuse its logs. Runs with an output file (--replay, --screenshot, --audio-capture) always run.
M=
[ -n "$RUN_NATIVE_MEMO" ] && [ -z "$EXTRA" ] &&
  M="$RUN_NATIVE_MEMO/$(echo "$FRAMES $TMO $EXPECT $* $(stat -f %m build/native/rugby08 2>/dev/null || stat -c %Y build/native/rugby08)" | cksum | cut -d" " -f1)"
rm -f logs/icall_miss.txt logs/native_run.log # this run's misses only (the game writes the file at its first miss)
if [ -n "$M" ] && [ -f "$M/rc" ]; then
  cp "$M/native_run.log" "$M/run_native.log" logs/
  [ ! -f "$M/icall_miss.txt" ] || cp "$M/icall_miss.txt" logs/
  RC=$(cat "$M/rc")
else
  SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-dummy} timeout -s KILL "$TMO" build/native/rugby08 --frames "$FRAMES" --log logs/native_run.log $EXTRA "$@" >logs/run_native.log 2>&1 &
  PID=$!
  if [ "$FRAMES" -eq 0 ]; then # no last frame: done once the line is there (not at the timeout)
    while kill -0 $PID 2>/dev/null && ! grep -qF -- "$EXPECT" logs/run_native.log logs/native_run.log; do sleep 0.2; done
    pkill -KILL -P $PID 2>/dev/null # the game (SDL turns SIGTERM into a quit event the game ignores)
  fi
  wait $PID
  RC=$?
  [ -z "$M" ] || { mkdir -p "$M" && cp logs/native_run.log logs/run_native.log "$M/" &&
    { [ ! -f logs/icall_miss.txt ] || cp logs/icall_miss.txt "$M/"; } && echo "$RC" >"$M/rc"; }
fi
if grep -qF -- "$EXPECT" logs/run_native.log logs/native_run.log; then # stdout to a file is buffered
  echo "run_native: ok ('$EXPECT' seen, exit $RC)"
  exit 0
fi
echo "run_native: FAIL ('$EXPECT' not seen, exit $RC; last: $(tail -1 logs/run_native.log | cut -c1-120))"
exit 1
