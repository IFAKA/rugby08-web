#!/bin/sh
# Netplay in the browser: the relay server (server/) serves build/web; two headless Chromium pages
# join the same room (?net=auto) and play in lockstep; both must print the same state hashes.
# Optional --native: the second player is the native build instead (cross-play: wasm vs native).
#   tools/net/net_web_test.sh [--hashes N] [--timeout S] [--native]
cd "$(dirname "$0")/../.." || exit 1
N=5 TMO=600 PORT=${PORT:-$(python3 tools/check/free_port.py)} NATIVE=0
while [ $# -gt 0 ]; do
  case "$1" in --hashes)
    N=$2
    shift 2
    ;;
  --timeout)
    TMO=$2
    shift 2
    ;;
  --native)
    NATIVE=1
    shift
    ;;
  *)
    echo "net_web_test: unknown $1"
    exit 2
    ;;
  esac
done
SHELL_BIN=$(ls -d "$HOME"/Library/Caches/ms-playwright/chromium_headless_shell-*/chrome-headless-shell-mac-*/chrome-headless-shell 2>/dev/null | tail -1)
HEADLESS=
[ -x "$SHELL_BIN" ] || {
  SHELL_BIN="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
  HEADLESS=--headless=new
}
(cd server && go build -o ../build/rugby08-server .) || {
  echo "net_web_test: FAIL (server build)"
  exit 1
}
build/rugby08-server -addr 127.0.0.1:$PORT -dir build/web >logs/net_web_server.log 2>&1 &
SRV=$!
for i in 1 2 3 4 5 6 7 8 9 10; do
  curl -sf -o /dev/null http://127.0.0.1:$PORT/index.html && break
  sleep 0.3
done
ROOM=W$$
page() { # $1 = player number
  mkdir -p "build/net_web_profile$1"
  "$SHELL_BIN" $HEADLESS --user-data-dir="$PWD/build/net_web_profile$1" --enable-logging=stderr --v=0 --no-first-run \
    --remote-debugging-port=0 --enable-unsafe-swiftshader --use-angle=swiftshader --mute-audio \
    "http://127.0.0.1:$PORT/index.html?log&net=auto&room=$ROOM&players=2&name=P$1" >"logs/net_web$1.log" 2>&1 &
}
page 1
B1=$!
if [ $NATIVE = 1 ]; then
  SDL_AUDIODRIVER=dummy build/native/rugby08 --frames 100000 --log logs/net_web_native.log --net ws://127.0.0.1:$PORT/ws \
    --net-room $ROOM --net-players 2 --net-name N >logs/net_web2.log 2>&1 &
  B2=$!
else
  page 2
  B2=$!
fi
trap 'kill $B1 $B2 $SRV 2>/dev/null' EXIT
hashes() { grep -o 'netplay: frame [0-9]* hash [0-9A-F]*' "$1" | head -"$N"; }
i=0
while [ $i -lt "$TMO" ]; do
  h1=$(hashes logs/net_web1.log | wc -l | tr -d ' ')
  h2=$(hashes logs/net_web2.log | wc -l | tr -d ' ')
  [ "$h1" -ge "$N" ] && [ "$h2" -ge "$N" ] && break
  grep -qh 'DESYNC' logs/net_web1.log logs/net_web2.log && break
  sleep 2
  i=$((i + 2))
done
hashes logs/net_web1.log >logs/net_web_hash1.txt
hashes logs/net_web2.log >logs/net_web_hash2.txt
d=$(grep -ho 'netplay: DESYNC[^"]*' logs/net_web1.log logs/net_web2.log | head -1)
if [ -z "$d" ] && [ "$(wc -l <logs/net_web_hash1.txt)" -ge "$N" ] && cmp -s logs/net_web_hash1.txt logs/net_web_hash2.txt; then
  echo "net_web_test: ok ($N state hashes equal$([ $NATIVE = 1 ] && echo ', browser vs native'), ${i}s)"
  exit 0
fi
echo "net_web_test: FAIL (${d:-hashes $(wc -l <logs/net_web_hash1.txt | tr -d ' ')/$(wc -l <logs/net_web_hash2.txt | tr -d ' ') of $N or differ} after ${i}s)"
exit 1
