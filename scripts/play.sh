#!/bin/sh
# Play the native macOS build (no frame limit; real mouse and keyboard). Builds it if missing.
#   scripts/play.sh [--mute] [extra rugby08 args, e.g. --audio-capture logs/play.wav]
#   --mute: SDL's dummy audio driver (no sound; the mixer still runs, so game timing is unchanged).
# Quit: close the window, or Ctrl-C in this terminal, or `pkill rugby08` from another one.
# Game log: logs/play-<time>.log per run (logs/play.log links to the newest).
cd "$(dirname "$0")/.." || exit 1
MUTE_ENV=
for a in "$@"; do
  case "$a" in
    --mute) MUTE_ENV=SDL_AUDIODRIVER=dummy ;;
    *) set -- "$@" "$a" ;;
  esac
  shift
done
# Always bring the build up to date (a no-op when nothing changed): the app bundle below is a copy,
# and a stale copy reopened from Finder runs old code.
if [ -d build/native ]; then
  cmake --build build/native -j >logs/play_build.log 2>&1 || {
    echo "play: build failed (logs/play_build.log)"
    exit 1
  }
else ./init.sh build-native || exit 1; fi

# Launch through an app bundle so macOS gives the game a real application
# identity, menu bar, Cmd-Tab entry, and keyboard focus.
APP=build/native/Rugby08.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" || exit 1
cp build/native/rugby08 "$APP/Contents/MacOS/Rugby08" || exit 1
printf 'APPL????' >"$APP/Contents/PkgInfo" || exit 1
cat >"$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>Rugby08</string>
    <key>CFBundleIdentifier</key>
    <string>local.rugby08.native</string>
    <key>CFBundleName</key>
    <string>Rugby 08</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleSupportedPlatforms</key>
    <array>
        <string>MacOSX</string>
    </array>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
</dict>
</plist>
PLIST

ROOT=$PWD
LOG=logs/play-$(date +%Y%m%d-%H%M%S).log
mkdir -p logs && : >"$LOG" && ln -sf "$(basename "$LOG")" logs/play.log
exec open -n ${MUTE_ENV:+--env "$MUTE_ENV"} "$APP" --args \
  --exe "$ROOT/Rugby08_unpacked.exe" \
  --game-dir "$ROOT/Game Files" \
  --user-dir "$ROOT/userdata" \
  --log "$ROOT/$LOG" --log-input "$@"
