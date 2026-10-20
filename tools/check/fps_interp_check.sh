#!/bin/sh
# fps_interp_check.sh -- fps.interp: a display faster than 60 Hz shows a real frame at every refresh
# between the game's steps, and the game still steps 60 times a second. Two paced deterministic native
# runs of a PLAY NOW match (tests/replays/match_play.txt, FRAMES default 16500: open play from 14400):
# plain (60 Hz), then on an emulated 144 Hz display (RT_TEST_HZ) with every shown frame's camera traced
# (RT_TRACE_INTERP). Pass: the guest state hashes are the same; over the last 30 seconds the 144 Hz run
# keeps 60 steps a second within 2% and draws at least 70 frames a second between them (144 - 60 = 84
# expected); and the camera moves smoothly: in at least 95% of the frames between two steps where it
# moved, it is where the frame's time puts it on the way between them (within 15%). One summary line;
# details in logs/fps_interp.log, runs in logs/fps_interp/.
cd "$(dirname "$0")/../.." || exit 1
LOG=logs/fps_interp.log OUT=logs/fps_interp BIN=${FPS_BIN:-build/native/rugby08} FRAMES=${FRAMES:-16500}
mkdir -p $OUT
: >$LOG
[ -x "$BIN" ] || {
  echo "fps_interp_check: FAIL ($BIN missing; ./init.sh build-native)"
  exit 1
}
run() { # name, extra environment
  rm -rf "$OUT/user.$1" && mkdir -p "$OUT/user.$1"
  env RT_DET=1 RT_DET_HASH=60 SDL_AUDIODRIVER=dummy $2 timeout 1500 "$BIN" --frames "$FRAMES" \
    --replay tests/replays/match_play.txt --log "$OUT/$1.log" --user-dir "$OUT/user.$1" >"$OUT/$1.out" 2>&1
  echo "$1: exit $?" >>$LOG
  grep '^det: frame' "$OUT/$1.out" | sed 's/ vt .*//' >"$OUT/$1.det"
  grep '^perf: [0-9]' "$OUT/$1.out" | tail -n 30 >"$OUT/$1.perf"
}
run plain ""
run hz144 "RT_TEST_HZ=144 RT_TRACE_INTERP=1"
NDET=$(wc -l <$OUT/plain.det | tr -d ' ')
cmp -s $OUT/plain.det $OUT/hz144.det && SAME=1 || SAME=0
# per second: steps/s ($4), frames between steps (the field before "frames between steps")
MED=$(awk '{ print $(NF - 3) }' $OUT/hz144.perf | sort -n | awk '{ b[NR] = $1 } END { print NR ? b[int((NR + 1) / 2)] : 0 }')
P=$(awk -v med="$MED" '{ s += $4 } END { printf "%.2f %d %d\n", NR ? s / NR : 0, med, NR }' $OUT/hz144.perf)
# smoothness: each frame's t and camera; a step's frames start at t = 0 (the step before it)
SM=$(grep '^interp eye' $OUT/hz144.out | awk '
  function judge(   i, dx, dy, dz, l2, s) {
    dx = x1 - g[0, 1]; dy = y1 - g[0, 2]; dz = z1 - g[0, 3]; l2 = dx * dx + dy * dy + dz * dz
    if (l2 < 1) return
    for (i = 1; i < gn; i++) {
      s = ((g[i, 1] - g[0, 1]) * dx + (g[i, 2] - g[0, 2]) * dy + (g[i, 3] - g[0, 3]) * dz) / l2
      n++; if (s - g[i, 0] < 0.15 && g[i, 0] - s < 0.15) ok++
    }
  }
  { t = $3 + 0; if (t == 0) { if (gn) { x1 = $4; y1 = $5; z1 = $6; judge() } gn = 0 }
    g[gn, 0] = t; g[gn, 1] = $4; g[gn, 2] = $5; g[gn, 3] = $6; gn++ }
  END { printf "%d %d\n", ok, n }')
REW=$(grep '^interp rewrites' $OUT/hz144.out | tail -n 1 | awk '{ print $3 + 0 " textures, " $5 + 0 " buffer copies" }')
echo "hz144: steps/s, median frames between steps, seconds: $P; camera on its way: $SM (ok, judged); rewrites $REW" >>$LOG
FAIL=$(echo "$P $SM" | awk -v same=$SAME -v ndet="$NDET" '{
  if (ndet < 30) print "too few hashes (" ndet ")"
  else if (!same) print "the state hashes differ"
  else if ($3 < 20) print "too few seconds measured"
  else if ($1 < 58.8 || $1 > 61.2) print "the 144 Hz run stepped " $1 "/s, not 60"
  else if ($2 < 70) print "only " $2 " frames a second between steps"
  else if ($5 < 1000) print "too few frames between moving steps (" $5 ")"
  else if ($4 < 0.95 * $5) print "the camera was off its way in " $5 - $4 " of " $5 " frames"
}')
if [ -n "$FAIL" ]; then
  echo "fps_interp_check: FAIL ($FAIL)" | tee -a $LOG
  exit 1
fi
echo "fps_interp_check: PASS (144 Hz: $(echo "$P" | cut -d' ' -f1) steps/s, $(echo "$P" | cut -d' ' -f2) frames a second between them, camera on its way in $(echo "$SM" | awk '{ printf "%.1f%%", 100 * $1 / $2 }'), $NDET hashes the same)" | tee -a $LOG
