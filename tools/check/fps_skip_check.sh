#!/bin/sh
# fps_skip_check.sh -- fps.skip: a device too slow to draw every step still steps the game 60 times a
# second, and its frames come at an even rate. Two paced deterministic native runs of a PLAY NOW match
# (tests/replays/match_play.txt, FRAMES default 12000): plain, then with every drawn frame costing 25 ms
# more (RT_TEST_DRAW_MS, a slow GPU: about 35 fps if every step were drawn). Pass: the guest state hashes
# are the same; the plain run draws every step; the slow run keeps 60 steps a second within 2% and draws
# one step in a steady N (every second's drawn frames within 2 of the median, which is below 45). Only
# the last 30 seconds (the match: the front end steps at the game's own 30 a second) are judged. One summary line; details in logs/fps_skip.log, runs in logs/fps_skip/.
cd "$(dirname "$0")/../.." || exit 1
LOG=logs/fps_skip.log OUT=logs/fps_skip BIN=${FPS_BIN:-build/native/rugby08} FRAMES=${FRAMES:-12000}
mkdir -p $OUT
: >$LOG
[ -x "$BIN" ] || {
  echo "fps_skip_check: FAIL ($BIN missing; ./init.sh build-native)"
  exit 1
}
run() { # name, extra ms per drawn frame
  rm -rf "$OUT/user.$1" && mkdir -p "$OUT/user.$1"
  RT_DET=1 RT_DET_HASH=60 RT_TEST_DRAW_MS=$2 SDL_AUDIODRIVER=dummy timeout 900 "$BIN" --frames "$FRAMES" \
    --replay tests/replays/match_play.txt \
    --log "$OUT/$1.log" --user-dir "$OUT/user.$1" >"$OUT/$1.out" 2>&1
  echo "$1: exit $?" >>$LOG
  grep '^det: frame' "$OUT/$1.out" | sed 's/ vt .*//' >"$OUT/$1.det"
  grep '^perf: [0-9]' "$OUT/$1.out" | tail -n 30 >"$OUT/$1.perf"
}
run plain 0
run slow 25
# per second: drawn fps ($2), steps/s ($4), not drawn ($6, "(N")
judge() { # name -> "mean_steps not_drawn_total median_fps fps_min fps_max seconds"
  sort -t' ' -k2,2n "$OUT/$1.perf" | awk '{ f[NR] = $2 } END { print f[int((NR + 1) / 2)] + 0 }' >"$OUT/$1.median"
  awk -v med="$(cat "$OUT/$1.median")" '{ s += $4; nd += substr($6, 2); n++; if (n == 1 || $2 < lo) lo = $2
    if ($2 > hi) hi = $2 } END { printf "%.2f %d %d %d %d %d\n", n ? s / n : 0, nd, med, lo, hi, n }' "$OUT/$1.perf"
}
P=$(judge plain) S=$(judge slow)
echo "plain: steps/s, not drawn, median fps, min, max, seconds: $P" >>$LOG
echo "slow:  steps/s, not drawn, median fps, min, max, seconds: $S" >>$LOG
NDET=$(wc -l <$OUT/plain.det | tr -d ' ')
cmp -s $OUT/plain.det $OUT/slow.det && SAME=1 || SAME=0
FAIL=$(echo "$P $S" | awk -v same=$SAME -v ndet="$NDET" -v load="$(uptime | sed 's/.*averages*: //')" '{
  if (ndet < 30) print "too few hashes (" ndet ")"
  else if (!same) print "the state hashes differ"
  else if ($6 < 20 || $12 < 20) print "too few seconds measured"
  else if ($3 < 55) print "the machine is too busy to judge: the plain run drew " $3 " fps (load " load ")"
  else if ($2 > 0) print "the plain run left " $2 " steps undrawn"
  else if ($7 < 58.8 || $7 > 61.2) print "the slow run stepped " $7 "/s, not 60"
  else if ($9 >= 45) print "the slow run drew " $9 " fps: no step left undrawn"
  else if ($10 < $9 - 2 || $11 > $9 + 2) print "the slow run drew " $10 "-" $11 " fps a second: not steady"
}')
if [ -n "$FAIL" ]; then
  echo "fps_skip_check: FAIL ($FAIL)" | tee -a $LOG
  exit 1
fi
echo "fps_skip_check: PASS (slow GPU: $(echo "$S" | cut -d' ' -f1) steps/s, $(echo "$S" | cut -d' ' -f3) fps steady, $NDET hashes the same)" | tee -a $LOG
