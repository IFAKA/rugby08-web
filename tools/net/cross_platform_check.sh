#!/bin/sh
# on.cross_platform: a Mac native game and a browser game (the web build, Playwright: tools/net/
# web_player.py) share one match, both ways, through the in-game Online screens and the relay.
#   A: the native host (tests/replays/ui_host.txt) hosts and plays; the browser friend types the code.
#   B: the browser host hosts and plays PLAY NOW; the native friend (tests/replays/ui_friend.txt) types it.
# Pass (tools/net/cross_platform_eval.py): each friend plays the host's teams and shows the host's frames
# (what its recorder writes is the host frame shown, within the replay coder's quantum). One summary line;
# details in logs/cross_platform/.
cd "$(dirname "$0")/../.." || exit 1
D=logs/cross_platform PORT=${PORT:-$(python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])')}
PY=/opt/homebrew/bin/python3 # Playwright
rm -rf "$D" && mkdir -p "$D/a" "$D/b"
(cd server && go build -o ../build/rugby08-server .) || {
  echo "cross_platform_check: FAIL (server build)"
  exit 1
}
build/rugby08-server -addr 127.0.0.1:$PORT -dir build/web >"$D/server.log" 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null' EXIT
sleep 1
kill -0 $SERVER 2>/dev/null || {
  echo "cross_platform_check: FAIL (relay did not start: $(tail -1 "$D/server.log"))"
  exit 1
}
native() { # dir, replay, frames
  RT_DET=1 RT_FPS=60 SDL_AUDIODRIVER=dummy RT_MS_POS=10 timeout -s KILL 1500 build/native/rugby08 \
    --net ws://127.0.0.1:$PORT/ws --user-dir "$1/u" --frames "$3" --replay "$2" --log "$1/native.log" >"$1/native.out" 2>&1
}
code_of() { # file, pattern
  for _ in $(seq 1 900); do
    c=$(sed -n "$2" "$1" 2>/dev/null | head -1)
    [ -n "$c" ] && echo "$c" && return 0
    sleep 1
  done
  return 1
}
friend_replay() { # code
  sed -n '/^[0-9]/p' tests/replays/ui_friend.txt
  echo "$1" | fold -w1 | awk '{ printf "%d tap %s\n", 9000 + 20 * NR, $1 }'
  echo "9200 tap Return"
  echo "9400 tap F2"
}

# A: native host, browser friend
sed "s|SHOTS|$D/a|" tests/replays/ui_host.txt >"$D/a/host.replay"
native "$D/a" "$D/a/host.replay" 40000 &
HOST=$!
CODE_A=$(code_of "$D/a/native.out" 's/^ms: ui room \([A-Z0-9]*\) as player 1 (host)$/\1/p') || {
  echo "cross_platform_check: FAIL (A: the native host got no room code)"
  exit 1
}
$PY tools/net/web_player.py "$PORT" "$D/a/web.log" join "$CODE_A" --secs 150 >"$D/a/web.out" 2>&1
kill $HOST 2>/dev/null
wait $HOST

# B: browser host, native friend
$PY tools/net/web_player.py "$PORT" "$D/b/web.log" host --play-now --secs 300 >"$D/b/web.out" 2>&1 &
WEB=$!
CODE_B=$(code_of "$D/b/web.out" 's/^web_player: room \([A-Z0-9]*\)$/\1/p') || {
  echo "cross_platform_check: FAIL (B: the browser host got no room code: $(tail -1 "$D/b/web.out"))"
  exit 1
}
friend_replay "$CODE_B" >"$D/b/friend.replay"
native "$D/b" "$D/b/friend.replay" 16000
wait $WEB
.venv/bin/python -I tools/net/cross_platform_eval.py "$D"
