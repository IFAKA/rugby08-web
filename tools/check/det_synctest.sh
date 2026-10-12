#!/bin/sh
# Deterministic-mode synctest: run the native build twice (in parallel) with the same replay in
# RT_DET=1 and require identical state hashes at every sampled frame. One summary line.
#   tools/check/det_synctest.sh [--frames N] [--every K] [--replay FILE] [--timeout S]
# Per-run game logs: logs/det_run{1,2}.log; hash streams: logs/det_hash{1,2}.txt.
# RT_DET_CHAOS=1 in the second run's environment perturbs host timing (det_chaos in det.c).
cd "$(dirname "$0")/../.." || exit 1
FRAMES=1200 EVERY=30 REPLAY=tests/replays/play_now_match.txt TMO=900 CHAOS=${CHAOS:-0}
while [ $# -gt 0 ]; do
  case "$1" in
    --frames)
      FRAMES=$2
      shift 2
      ;;
    --every)
      EVERY=$2
      shift 2
      ;;
    --replay)
      REPLAY=$2
      shift 2
      ;;
    --timeout)
      TMO=$2
      shift 2
      ;;
    --chaos)
      CHAOS=1
      shift
      ;;
    *)
      echo "det_synctest: unknown option $1"
      exit 2
      ;;
  esac
done
run() { # $1 = run number, $2 = chaos
  RT_DET=1 RT_DET_HASH=$EVERY RT_DET_CHAOS=$2 RT_FPS=0 SDL_AUDIODRIVER=dummy timeout -s KILL "$TMO" \
    build/native/rugby08 --frames "$FRAMES" --replay "$REPLAY" --log "logs/det_run$1.log" >"logs/det_out$1.log" 2>&1
  grep '^det: frame' "logs/det_out$1.log" | sed 's/ vt .*//' >"logs/det_hash$1.txt"
}
run 1 0 &
run 2 "$CHAOS" &
wait
n1=$(wc -l <logs/det_hash1.txt) n2=$(wc -l <logs/det_hash2.txt)
want=$((FRAMES / EVERY))
first=$(diff logs/det_hash1.txt logs/det_hash2.txt | grep '^<' | head -1 | cut -c3-)
if [ "$n1" -ge "$want" ] && [ "$n2" -ge "$want" ] && [ -z "$first" ]; then
  echo "det_synctest: ok ($n1 hashes equal up to frame $FRAMES${CHAOS:+, chaos=$CHAOS})"
  exit 0
fi
echo "det_synctest: FAIL (hashes $n1/$n2 of $want; first difference: ${first:-none}; last: $(tail -1 logs/det_out1.log | cut -c1-100))"
exit 1
