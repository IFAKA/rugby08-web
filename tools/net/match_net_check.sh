#!/bin/sh
# on.match_net: host-authoritative online play over the relay (server/, runtime/core/net_ha.c). The host
# (tests/replays/match_play.txt) plays; the friend (tests/replays/friend_ha.txt: its own menu route, its pad
# joining the away side on the host and running in open play) sends its pad and shows the host's frames.
# Both native, deterministic and paced at 60 fps (latency in frames needs real time). Pass
# (tools/net/match_net_eval.py): the friend plays the host's match (teams, stadium; what its recorder
# writes is the host frame it shows within the replay coder's quantum over >= 3000 frames; the host's
# score), host -> friend latency p95 <= 6 frames, and the friend's pad moves its player on the host.
# One summary line; details in logs/match_net/.
cd "$(dirname "$0")/../.." || exit 1
D=logs/match_net PORT=${PORT:-$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')} ROOM=ha$$
rm -rf "$D" && mkdir -p "$D"
(cd server && go build -o ../build/rugby08-server .) || {
  echo "match_net_check: FAIL (server build)"
  exit 1
}
build/rugby08-server -addr 127.0.0.1:$PORT -dir build/web >"$D/server.log" 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT
sleep 1
kill -0 $SERVER 2>/dev/null || { # e.g. the port is taken: never test against another relay
  echo "match_net_check: FAIL (relay did not start: $(tail -1 "$D/server.log"))"
  exit 1
}
run() { # name, replay, frames, extra env...
  name=$1 replay=$2 frames=$3
  shift 3
  grep -v ' shot ' "$replay" >"$D/$name.replay"
  env "$@" RT_DET=1 RT_FPS=60 SDL_AUDIODRIVER=dummy timeout -s KILL 1500 build/native/rugby08 --user-dir "$D/u_$name" \
    --frames "$frames" --replay "$D/$name.replay" --log "$D/$name.log" --net ws://127.0.0.1:$PORT/ws \
    --net-room "$ROOM" --net-mode ha --net-name "$name" >"$D/$name.out" 2>&1
}
run host tests/replays/match_play.txt 19000 RT_MS_POS=1 RT_MS_LOG=1 &
HOST=$!
sleep 2
run friend tests/replays/friend_ha.txt 19500 RT_MS_POS=1 RT_NET_HA_PRESS=180 &
FRIEND=$!
wait $HOST $FRIEND
.venv/bin/python -I tools/net/match_net_eval.py "$D"
