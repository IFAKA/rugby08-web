#!/bin/sh
# on.ingame_ui: online play from the in-game Online screens (runtime/core/ui_online.c). The host
# opens them (F2), hosts a room and closes them, then plays its menus into a match (tests/replays/
# ui_host.txt); the code it was given is read from its trace and typed by a friend on its Online screens
# (JOIN A GAME, the letters, Return; its replay is written once the code exists, so it joins while the
# host already plays: the relay hands it the match's setup and last key), and by a second friend in a
# browser (tools/net/ingame_ui_web.py: the web build, keys typed by Playwright). Pass
# (tools/net/ingame_ui_eval.py): the code is drawn in the host's picture, the relay puts each friend in
# the host's room and its screens show them, and the native friend follows the host into its match. One
# summary line; details in logs/ingame_ui/.
cd "$(dirname "$0")/../.." || exit 1
D=logs/ingame_ui PORT=${PORT:-$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')}
rm -rf "$D" && mkdir -p "$D"
(cd server && go build -o ../build/rugby08-server .) || {
  echo "ingame_ui_check: FAIL (server build)"
  exit 1
}
build/rugby08-server -addr 127.0.0.1:$PORT -dir build/web >"$D/server.log" 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT
sleep 1
kill -0 $SERVER 2>/dev/null || {
  echo "ingame_ui_check: FAIL (relay did not start: $(tail -1 "$D/server.log"))"
  exit 1
}
run() { # name, replay, frames
  RT_DET=1 RT_FPS=60 SDL_AUDIODRIVER=dummy RT_MS_POS=10 timeout -s KILL 1200 build/native/rugby08 \
    --net ws://127.0.0.1:$PORT/ws --user-dir "$D/u_$1" --frames "$3" --replay "$2" --log "$D/$1.log" >"$D/$1.out" 2>&1
}
sed "s|SHOTS|$D|" tests/replays/ui_host.txt >"$D/host.replay"
run host "$D/host.replay" 40000 &
HOST=$!
for _ in $( # the host's code (it hosts at frame ~8650, ~2.5 minutes in at 60 fps)
  seq 1 600
); do
  CODE=$(sed -n 's/^ms: ui room \([A-Z0-9]*\) as player 1 (host)$/\1/p' "$D/host.out" 2>/dev/null | head -1)
  [ -n "$CODE" ] && break
  sleep 1
done
[ -n "$CODE" ] || {
  echo "ingame_ui_check: FAIL (the host got no room code)"
  exit 1
}
{
  sed -n '/^[0-9]/p' tests/replays/ui_friend.txt
  echo "$CODE" | fold -w1 | awk '{ printf "%d tap %s\n", 9000 + 20 * NR, $1 }'
  echo "9200 tap Return"
  echo "9400 tap F2"
} >"$D/friend.replay"
run friend "$D/friend.replay" 11800
# then a second friend in a browser (the web build, served by the relay; Playwright: system Python), with
# the host only (headless Chrome is slow on a loaded machine)
/opt/homebrew/bin/python3 tools/net/ingame_ui_web.py "$PORT" "$CODE" "$D/web.log" >"$D/web.out" 2>&1
kill $HOST 2>/dev/null
wait $HOST
.venv/bin/python -I tools/net/ingame_ui_eval.py "$D" "$CODE"
