#!/bin/sh
# on.match_flow: a party of a host and a friend runs the whole flow twice (runtime/core/net_ha_flow.c).
# The host (tests/replays/host_flow.txt) starts a PLAY NOW match; the friend, idle in its own main menu
# (tests/replays/friend_party.txt), is taken into it, leaves its intro when the host plays, pauses the
# match from its side in open play and resumes it with its pad (RT_NET_HA_PAUSE), and answers its side's
# set-piece prompts (RT_NET_HA_MASH: the host waits for them). The host quits at match
# time T (RT_MS_QUIT_AT), the friend follows it back to its menus; the host starts the same match again
# (RT_MS_RELAUNCH) and the friend follows again. Pass (tools/net/match_flow_eval.py): two launches on each
# side with the host's setup, the friend shows the host's frames in each match, the pause stops the host's
# clock until the friend's resume, and both quits bring the friend back to its menus. One summary line;
# details in logs/match_flow/.
cd "$(dirname "$0")/../.." || exit 1
D=logs/match_flow PORT=${PORT:-$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')} ROOM=flow$$ T=40
rm -rf "$D" && mkdir -p "$D"
(cd server && go build -o ../build/rugby08-server .) || {
  echo "match_flow_check: FAIL (server build)"
  exit 1
}
build/rugby08-server -addr 127.0.0.1:$PORT -dir build/web >"$D/server.log" 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT
sleep 1
kill -0 $SERVER 2>/dev/null || { # e.g. the port is taken: never test against another relay
  echo "match_flow_check: FAIL (relay did not start: $(tail -1 "$D/server.log"))"
  exit 1
}
run() { # name, replay, frames, extra env...
  name=$1 replay=$2 frames=$3
  shift 3
  cp "$replay" "$D/$name.replay"
  env "$@" RT_DET=1 RT_FPS=60 SDL_AUDIODRIVER=dummy RT_MS_POS=10 timeout -s KILL 1500 build/native/rugby08 \
    --user-dir "$D/u_$name" --frames "$frames" --replay "$D/$name.replay" --log "$D/$name.log" \
    --net ws://127.0.0.1:$PORT/ws --net-room "$ROOM" --net-mode ha --net-name "$name" >"$D/$name.out" 2>&1
}
run host tests/replays/host_flow.txt 20500 RT_MS_LOG=1 RT_MS_QUIT_AT=$T RT_MS_RELAUNCH=300 &
HOST=$!
sleep 2
run friend tests/replays/friend_party.txt 20500 RT_NET_HA_PAUSE=150 RT_NET_HA_MASH=120 &
FRIEND=$!
wait $HOST $FRIEND
.venv/bin/python -I tools/net/match_flow_eval.py "$D"
