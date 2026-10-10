#!/bin/sh
# oracle.wine / deprotect.exe: run a game exe under Wine (build/wine-dl, wine-stable 11.0 from
# Gcenx/macOS_Wine_builds) as the reference for the original code. One summary line.
#   tools/run_wine.sh EXE [--seconds N] [--tries N] [--show] [--sound] [--keep]
# Muted by default (winecoreaudio.drv disabled for the run, so DirectSound has no device);
# --sound or --keep (to play) keep the audio.
# By default the game window is minimized without focus and Wine does not grab the mouse
# (prefix: GrabPointer/GrabFullscreen=N, DirectInput MouseWarpOverride=disable), so runs do not
# block the user's screen. --show keeps it visible; --keep (implies --show) leaves it running to play.
# PASS = the process is still alive after N seconds (default 60), a D3D device was created (proxy
# "forced windowed: ok"), and Wine reported no unhandled exception. --tries N (default 1) reruns a
# run that died: about half the launches under Wine/Rosetta die ~3 s in, right after
# NtQuerySystemInformation(SYSTEM_PERFORMANCE_INFORMATION), before any window (see PROGRESS 2026-10-10). --keep leaves the game running (for the user to play).
# Prefix: build/wineprefix. Game dir: build/wine-game (symlinks to "Game Files" + EXE, plus
# build/oracle/d3d8.dll from tools/oracle/build_proxy.sh, which forces a windowed device: macOS has
# no 640x480 16-bit fullscreen mode).
# Wine debug output: logs/wine.log (channels: $RUN_WINE_DEBUG, default +loaddll,+seh,err+all).
cd "$(dirname "$0")/.." || exit 1
ROOT=$(pwd)
EXE=$1; shift
SECS=60 KEEP=0 SHOW=0 TRIES=1 SOUND=0
while [ $# -gt 0 ]; do
  case "$1" in
    --seconds) SECS=$2; shift 2;;
    --keep) KEEP=1; SHOW=1; SOUND=1; shift;;
    --sound) SOUND=1; shift;;
    --show) SHOW=1; shift;;
    --tries) TRIES=$2; shift 2;;
    *) echo "run_wine: unknown arg $1"; exit 2;;
  esac
done
[ -f "$EXE" ] || { echo "run_wine: FAIL (no exe: $EXE)"; exit 2; }
WBIN="$ROOT/build/wine-dl/Wine Stable.app/Contents/Resources/wine/bin"
[ -x "$WBIN/wine" ] || { echo "run_wine: FAIL (Wine missing: see docs/plans/2026-10-08-oracle-fallback-deprotect.md P1)"; exit 2; }
export PATH="$WBIN:$PATH" WINEPREFIX="$ROOT/build/wineprefix" WINEDLLOVERRIDES="mscoree,mshtml=;d3d8=n,b"
[ -f build/oracle/d3d8.dll ] || tools/oracle/build_proxy.sh >/dev/null || { echo "run_wine: FAIL (proxy build; see logs/build_proxy.log)"; exit 2; }
mkdir -p logs build/wine-game
LOG=logs/wine.log
if [ ! -f "$WINEPREFIX/system.reg" ]; then
  wineboot -u >logs/wine_boot.log 2>&1; wineserver -w
fi
# Game dir: every "Game Files" entry symlinked, plus the exe under its own name.
for f in "$ROOT/Game Files"/*; do ln -sfn "$f" "build/wine-game/$(basename "$f")"; done
ln -sfn "$ROOT/build/oracle/d3d8.dll" build/wine-game/d3d8.dll
NAME=$(basename "$EXE")
ln -sfn "$ROOT/$EXE" "build/wine-game/$NAME"
[ $SHOW -eq 1 ] || export RUGBY08_ORACLE_BACKGROUND=1
[ $SOUND -eq 1 ] || WINEDLLOVERRIDES="$WINEDLLOVERRIDES;winecoreaudio.drv=d"
t=0
while [ $t -lt "$TRIES" ]; do
  t=$((t+1))
  cd "$ROOT/build/wine-game" || exit 1
  WINEDEBUG=${RUN_WINE_DEBUG:-+loaddll,+seh,err+all} wine "$NAME" >"$ROOT/$LOG" 2>&1 &
  PID=$!
  cd "$ROOT" || exit 1
  i=0; while [ $i -lt "$SECS" ] && kill -0 $PID 2>/dev/null; do sleep 1; i=$((i+1)); done
  ALIVE=0; kill -0 $PID 2>/dev/null && ALIVE=1
  [ $KEEP -eq 1 ] && [ $ALIVE -eq 1 ] || { wineserver -k 2>/dev/null; wait $PID 2>/dev/null; }
  DEV=$(grep -c 'forced windowed: ok' $LOG)
  UNH=$(grep -m1 -iE 'Unhandled (page fault|exception)|wine: Unhandled|unable to dispatch exception' $LOG | cut -c1-140)
  if [ $ALIVE -eq 1 ] && [ "$DEV" -gt 0 ] && [ -z "$UNH" ]; then
    echo "run_wine: ok ($NAME alive ${SECS}s, $DEV device creations, no unhandled exception; try $t/$TRIES)"; exit 0
  fi
  echo "try $t: alive=$ALIVE after ${i}s, devices=$DEV, unhandled='${UNH:-none}'" >> logs/run_wine.log
done
echo "run_wine: FAIL ($NAME alive=$ALIVE after ${i}s, devices=$DEV, unhandled='${UNH:-none}'; $TRIES tries; see $LOG)"; exit 1
