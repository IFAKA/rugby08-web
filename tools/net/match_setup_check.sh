#!/bin/sh
# on.match_setup: a friend whose own game took another menu route plays the host's match. The host
# (tests/replays/match_play.txt: PLAY NOW, Scotland v New Zealand, Murrayfield) records its setup at
# launch (runtime/core/match_setup.c); the friend (tests/replays/friend_my_rugby.txt: MY RUGBY first,
# then PLAY NOW with other teams and another stadium) loads it at its own launch. Pass: the friend's
# setup bytes, teams and stadium equal the host's, and its picture 1000 frames after launch is the
# host's match (much closer to the host's than the friend's own match is). One summary line.
# Details: logs/match_setup/.
cd "$(dirname "$0")/../.." || exit 1
D=logs/match_setup
rm -rf "$D" && mkdir -p "$D/host" "$D/own" "$D/friend"
K=1000
run() { # name, replay, extra env...
  name=$1 replay=$2
  shift 2
  grep -v ' shot ' "$replay" >"$D/$name.replay"
  env "$@" RT_DET=1 RT_FPS=0 RT_MS_AFTER_LAUNCH="$K:$D/$name.png" SDL_AUDIODRIVER=dummy timeout -s KILL 1500 \
    build/native/rugby08 --user-dir "$D/$name" --frames 18000 --replay "$D/$name.replay" --log "$D/$name.log" \
    >"$D/$name.out" 2>&1
}
run host tests/replays/match_play.txt RT_MS_SETUP_OUT="$D/host.setup" &
run own tests/replays/friend_my_rugby.txt &
wait
[ -s "$D/host.setup" ] || {
  echo "match_setup_check: FAIL (the host never launched a match: $(tail -1 $D/host.out | cut -c1-100))"
  exit 1
}
run friend tests/replays/friend_my_rugby.txt RT_MS_SETUP_IN="$D/host.setup"
.venv/bin/python -I tools/net/match_setup_eval.py "$D"
