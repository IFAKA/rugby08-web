#!/bin/sh
# aems.rebase: leaving a match never stops the game's sound (runtime/core/aems.c). The game loads EA's
# sound banks (code) into the heap and loads some again elsewhere after a match (the menus' fe_sfx.abk):
# a bank must be found where it is now. A PLAY NOW match (tests/replays/play_now_match.txt), quit at
# frame 16000 (RT_QUIT_AT_FRAME, runtime/debug/quit_at.c), then the menus' sounds for 6000 frames (Down presses). Pass: the
# bank was found at its new place ("AEMS: fe_sfx.abk moved"), no unresolved call, the last frame reached.
# Hidden and muted; one summary line, details in logs/quit_sound/.
cd "$(dirname "$0")/../.." || exit 1
D=logs/quit_sound
rm -rf "$D" && mkdir -p "$D"
{
  grep -v " shot " tests/replays/play_now_match.txt
  f=17000
  while [ $f -le 23000 ]; do
    echo "$f tap Down"
    f=$((f + 150))
  done
} >"$D/replay.txt"
RT_DET=1 SDL_AUDIODRIVER=dummy RT_QUIT_AT_FRAME=16000 timeout -s KILL 1500 build/native/rugby08 --user-dir "$D/u" \
  --frames 23500 --replay "$D/replay.txt" --log "$D/game.log" >"$D/out.log" 2>&1
RC=$?
if ! grep -q "ms: game state 008CD620 at frame 1[6-9]" "$D/game.log"; then
  echo "quit_sound_check: FAIL (the match was not quit back to the menus; exit $RC)"
  exit 1
fi
BAD=$(grep -cE "ICALL: unresolved|out of bounds|unreachable" "$D/game.log" "$D/out.log" | awk -F: '{s += $NF} END {print s}')
if [ "$BAD" -gt 0 ] || ! grep -q "AEMS: fe_sfx.abk moved" "$D/game.log" || ! grep -q "frame limit 23500" "$D/out.log"; then
  echo "quit_sound_check: FAIL ($BAD errors; $(grep -c 'AEMS: fe_sfx.abk moved' "$D/game.log") bank moves; exit $RC)"
  exit 1
fi
echo "quit_sound_check: ok (match quit, fe_sfx.abk found at its new place, 6000 frames of menus, no errors)"
