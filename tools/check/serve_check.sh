#!/bin/sh
# web.serve check: serve build/web on a free local port and verify headers + wasm MIME. One summary line.
cd "$(dirname "$0")/../.." || exit 1
PORT=${PORT:-$(python3 tools/check/free_port.py)}
python3 tools/web/serve.py build/web $PORT >logs/serve_check.log 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null' EXIT
for _ in 1 2 3 4 5 6 7 8 9 10; do
  curl -sf -o /dev/null http://127.0.0.1:$PORT/index.html && break
  sleep 0.5
done
H=$(curl -sI http://127.0.0.1:$PORT/index.html)
W=$(curl -sI http://127.0.0.1:$PORT/rugby08.wasm)
echo "$H" >>logs/serve_check.log
echo "$W" >>logs/serve_check.log
echo "$H" | grep -qi '^cross-origin-opener-policy: same-origin' &&
  echo "$H" | grep -qi '^cross-origin-embedder-policy: require-corp' &&
  echo "$W" | grep -qi '^content-type: application/wasm' &&
  {
    echo "serve_check: ok (COOP/COEP + wasm MIME on 127.0.0.1:$PORT)"
    exit 0
  }
echo "serve_check: FAIL (see logs/serve_check.log)"
exit 1
