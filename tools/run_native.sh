#!/bin/sh
# native.* checks: run build/native/rugby08 and require a trace line on its stdout. One summary line.
#   tools/run_native.sh [--frames N] [--timeout S] --expect 'TEXT' [extra rugby08 args...]
# Game log: logs/native_run.log; stdout/stderr of the run: logs/run_native.log.
cd "$(dirname "$0")/.." || exit 1
FRAMES=0 TMO=120 EXPECT=
while [ $# -gt 0 ]; do
  case "$1" in
    --frames) FRAMES=$2; shift 2;;
    --timeout) TMO=$2; shift 2;;
    --expect) EXPECT=$2; shift 2;;
    *) break;;
  esac
done
[ -n "$EXPECT" ] || { echo "run_native: --expect required"; exit 2; }
[ -x build/native/rugby08 ] || { echo "run_native: FAIL (build/native/rugby08 missing; ./init.sh build-native)"; exit 1; }
timeout "$TMO" build/native/rugby08 --frames "$FRAMES" --log logs/native_run.log "$@" >logs/run_native.log 2>&1
RC=$?
if grep -qF -- "$EXPECT" logs/run_native.log; then
  echo "run_native: ok ('$EXPECT' seen, exit $RC)"; exit 0
fi
echo "run_native: FAIL ('$EXPECT' not seen, exit $RC; last: $(tail -1 logs/run_native.log | cut -c1-120))"; exit 1
