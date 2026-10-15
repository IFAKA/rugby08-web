#!/bin/sh
# dev.sh (npm run dev) -- from a fresh clone to the game in your browser, with your own copy of Rugby 08.
# Each step runs only when its output is missing or old, so a second run starts the game in seconds:
#   1 your game: "Game Files/" (the installed game's folder) and Rugby08_unpacked.exe (checked against
#     analysis/exe.sha256: the function list in analysis/ is for that exe)
#   2 tools: python3, cmake, emcc (macOS: brew install python cmake emscripten), a .venv for the lifter
#   3 third_party/ (tools/setup_submodules.sh; Wine's Win32 signatures: tools/analysis/fetch_wine_specs.sh)
#   4 gen/: the exe recompiled to C (tools/lift/lift.py, seconds) and its sound-bank code (aems_lift.py)
#   5 build/web/data/: the game files in 8 MiB chunks (tools/web/package_assets.py)
#   6 build/web/: the browser build (./init.sh build-web; the first one takes ~5 minutes)
#   7 http://localhost:8080 (opened for you; installable as an app)
# PORT=N picks another port; NO_OPEN=1 does not open a browser (checks). One line per step; details in logs/.
cd "$(cd "$(dirname "$0")/.." && pwd -P)" || exit 1 # (the real path: Emscripten builds its libraries by relative paths)
mkdir -p logs
PORT=${PORT:-8080}
say() { printf '\033[1m%s\033[0m %s\n' "dev:" "$*"; }
die() {
  say "$*"
  exit 1
}

# 1 your game
[ -d "Game Files" ] || die "put your Rugby 08 game folder here as 'Game Files/' (the folder with Audio/, Data/ ...)"
[ -f Rugby08_unpacked.exe ] || die "put the game's executable here as Rugby08_unpacked.exe (without its disc protection)"
shasum -a 256 -c analysis/exe.sha256 >logs/dev_exe.log 2>&1 ||
  die "Rugby08_unpacked.exe is not the version this repo was built for (analysis/exe.sha256, logs/dev_exe.log)"
say "your game: ok"

# 2 tools
for t in python3 cmake emcc; do
  command -v "$t" >/dev/null || die "missing '$t' (macOS: brew install python cmake emscripten)"
done
if [ ! -f .venv/requirements.txt ] || ! cmp -s requirements.txt .venv/requirements.txt; then
  { [ -x .venv/bin/python ] || python3 -m venv .venv; } && .venv/bin/pip install -q -r requirements.txt >logs/dev_venv.log 2>&1 &&
    cp requirements.txt .venv/requirements.txt || die "python packages failed (logs/dev_venv.log)"
fi
say "tools: ok"

# 3 third_party
if [ ! -f third_party/pcrecomp/tools/lift/lift32.py ] || [ ! -f third_party/mojoshader/mojoshader.c ]; then
  tools/setup_submodules.sh || exit 1
fi
if [ ! -s third_party/wine-specs/kernel32.spec ]; then # (Win32 import signatures, for the import bridges)
  tools/analysis/fetch_wine_specs.sh || exit 1
fi

# 4 gen/
G=gen/recomp/recomp_funcs.h
if [ ! -f $G ] || [ analysis/lift_catalog.json -nt $G ] || [ -n "$(find tools/lift -name '*.py' -newer $G | head -1)" ]; then
  .venv/bin/python tools/lift/lift.py >logs/dev_lift.log 2>&1 || die "recompiling the exe failed (logs/dev_lift.log)"
  say "$(tail -1 logs/dev_lift.log)"
fi
if [ ! -f gen/aems/aems.c ]; then
  .venv/bin/python tools/lift/aems_lift.py >logs/dev_aems.log 2>&1 || die "sound-bank code failed (logs/dev_aems.log)"
  say "$(tail -1 logs/dev_aems.log)"
fi

# 5 game data
if [ ! -f build/web/data/manifest.json ]; then
  say "packaging your game files (1.7 GB, once)..."
  python3 tools/web/package_assets.py "Game Files" build/web/data || die "packaging failed (logs/package_assets.log)"
fi

# 6 the browser build (incremental: seconds when nothing changed)
say "building for the browser (the first build takes a few minutes)..."
./init.sh build-web || die "build failed (logs/web_build.log)"

# 7 play
URL="http://localhost:$PORT/"
say "playing at $URL (Ctrl+C to stop)"
[ -n "$NO_OPEN" ] || (sleep 1 && { command -v open >/dev/null && open "$URL" || xdg-open "$URL"; } >/dev/null 2>&1) &
exec python3 tools/web/serve.py build/web "$PORT"
