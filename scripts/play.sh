#!/bin/sh
# Play the native macOS build (no frame limit; real mouse and keyboard). Builds it if missing.
#   scripts/play.sh [extra rugby08 args, e.g. --audio-capture logs/play.wav]
# Quit: close the window, or Ctrl-C in this terminal, or `pkill rugby08` from another one.
# Game log: logs/play.log.
cd "$(dirname "$0")/.." || exit 1
[ -x build/native/rugby08 ] || ./init.sh build-native || exit 1

# Launch through an app bundle so macOS gives the game a real application
# identity, menu bar, Cmd-Tab entry, and keyboard focus.
APP=build/native/Rugby08.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources" || exit 1
cp build/native/rugby08 "$APP/Contents/MacOS/Rugby08" || exit 1
printf 'APPL????' > "$APP/Contents/PkgInfo" || exit 1
cat > "$APP/Contents/Info.plist" <<'PLIST'
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
exec open -n "$APP" --args \
    --exe "$ROOT/Rugby08_unpacked.exe" \
    --game-dir "$ROOT/Game Files" \
    --user-dir "$ROOT/userdata" \
    --log "$ROOT/logs/play.log" --log-input "$@"
