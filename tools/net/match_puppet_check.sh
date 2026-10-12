#!/bin/sh
# on.match_puppet: a friend's game plays the host's match from the host's frames, its own match logic
# stopped (runtime/core/match_puppet.c, game/overrides/match/). The host (tests/replays/match_play.txt,
# scores 0-3 and 0-10) records its setup and its frames (RT_MS_REC_OUT, the stand-in for the network);
# the friend (tests/replays/friend_my_rugby.txt, another menu route) loads both; a control friend loads
# only the setup and plays its own match. Pass (tools/net/match_puppet_eval.py): over >= 3000 frames
# what the friend's own recorder writes (every player's position and heading, the ball) is the host's
# within 1 cm, the friend ends with the host's score, and at fixed match times its picture is much
# closer to the host's than the control's is. One summary line; details in logs/match_puppet/.
cd "$(dirname "$0")/../.." || exit 1
D=logs/match_puppet
rm -rf "$D" && mkdir -p "$D/host" "$D/friend" "$D/control"
T=5,10,20,30,40,50,60,80,100
run() { # name, replay, frames, extra env...
  name=$1 replay=$2 frames=$3
  shift 3
  grep -v ' shot ' "$replay" >"$D/$name.replay"
  env "$@" RT_DET=1 RT_FPS=0 RT_MS_POS=1 RT_MS_SHOT_T="$D/$name:$T" SDL_AUDIODRIVER=dummy timeout -s KILL 2400 \
    build/native/rugby08 --user-dir "$D/$name" --frames "$frames" --replay "$D/$name.replay" --log "$D/$name.log" \
    >"$D/$name.out" 2>&1
}
run host tests/replays/match_play.txt 30000 RT_MS_SETUP_OUT="$D/host.setup" RT_MS_REC_OUT="$D/host.rec"
[ -s "$D/host.rec" ] || {
  echo "match_puppet_check: FAIL (the host recorded no match: $(tail -1 $D/host.out | cut -c1-100))"
  exit 1
}
run friend tests/replays/friend_my_rugby.txt 32300 RT_MS_SETUP_IN="$D/host.setup" RT_MS_PUPPET_IN="$D/host.rec" &
run control tests/replays/friend_my_rugby.txt 32300 RT_MS_SETUP_IN="$D/host.setup" &
wait
.venv/bin/python -I tools/net/match_puppet_eval.py "$D" "$T"
