#!/bin/sh
# Play the native macOS build (no frame limit; real mouse and keyboard). Builds it if missing.
#   scripts/play.sh [extra rugby08 args, e.g. --audio-capture logs/play.wav]
# Quit: close the window, or Ctrl-C in this terminal, or `pkill rugby08` from another one.
# Game log: logs/play.log.
cd "$(dirname "$0")/.." || exit 1
[ -x build/native/rugby08 ] || ./init.sh build-native || exit 1
exec build/native/rugby08 --log logs/play.log "$@"
