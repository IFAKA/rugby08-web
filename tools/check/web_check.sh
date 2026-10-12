#!/bin/sh
# web.* checks: serve build/web on localhost (tools/web/serve.py, COOP/COEP) and load it in a headless
# Chromium (Playwright's cached chrome-headless-shell, else Google Chrome --headless=new: no window,
# no sound, a throwaway profile), then require a line in
# the page's console. One summary line; console and browser log in logs/web_check.log.
#   tools/check/web_check.sh --expect 'TEXT' [--timeout S]
# The game's stdout (rt_trace, e.g. tools/hooks.json "WinMain entered") reaches console.log.
cd "$(dirname "$0")/../.." || exit 1
EXPECT='' TMO=120 PORT=18081
while [ $# -gt 0 ]; do
  case "$1" in
    --expect)
      EXPECT=$2
      shift 2
      ;;
    --timeout)
      TMO=$2
      shift 2
      ;;
    *)
      echo "web_check: unknown arg $1"
      exit 2
      ;;
  esac
done
[ -n "$EXPECT" ] || {
  echo "web_check: --expect required"
  exit 2
}
[ -f build/web/rugby08.wasm ] || {
  echo "web_check: FAIL (build/web/rugby08.wasm missing; ./init.sh build-web)"
  exit 1
}
SHELL_BIN=$(ls -d "$HOME"/Library/Caches/ms-playwright/chromium_headless_shell-*/chrome-headless-shell-mac-*/chrome-headless-shell 2>/dev/null | tail -1)
HEADLESS=
if [ ! -x "$SHELL_BIN" ]; then # else the installed Chrome in headless mode, with its own throwaway profile
  SHELL_BIN="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
  HEADLESS=--headless=new
fi
[ -x "$SHELL_BIN" ] || {
  echo "web_check: FAIL (no Playwright chrome-headless-shell and no Google Chrome)"
  exit 1
}
LOG=logs/web_check.log
mkdir -p logs build/web_check_profile
python3 tools/web/serve.py build/web $PORT >logs/web_check_serve.log 2>&1 &
SRV=$!
for i in 1 2 3 4 5 6 7 8 9 10; do
  curl -sf -o /dev/null http://127.0.0.1:$PORT/index.html && break
  sleep 0.5
done
"$SHELL_BIN" $HEADLESS $CHROME_FLAGS --user-data-dir="$PWD/build/web_check_profile" --enable-logging=stderr --v=0 --no-first-run \
  --remote-debugging-port=0 --enable-unsafe-swiftshader --use-angle=swiftshader --mute-audio \
  "http://127.0.0.1:$PORT/index.html?log" >"$LOG" 2>&1 &
BR=$!
trap 'kill $BR $SRV 2>/dev/null' EXIT
i=0
while [ $i -lt "$TMO" ]; do
  grep -qF -- "$EXPECT" "$LOG" && {
    echo "web_check: ok ('$EXPECT' in the page console after ${i}s)"
    exit 0
  }
  kill -0 $BR 2>/dev/null || break
  sleep 1
  i=$((i + 1))
done
LAST=$(grep -E 'CONSOLE|ERROR' "$LOG" | tail -1 | cut -c1-140)
echo "web_check: FAIL ('$EXPECT' not seen after ${i}s; last: ${LAST:-none}; see $LOG)"
exit 1
