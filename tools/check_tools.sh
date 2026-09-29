#!/usr/bin/env bash
# One-line summary; details in logs/check_tools.log
cd "$(dirname "$0")/.." || exit 1
log=logs/check_tools.log; mkdir -p logs; : > "$log"; miss=()
chk(){ if eval "$2" >>"$log" 2>&1; then echo "ok  $1" >>"$log"; else echo "MISS $1" >>"$log"; miss+=("$1"); fi; }
chk jdk21     '/opt/homebrew/opt/openjdk@21/bin/java -version'
chk ghidra    'test -x /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless'
chk emcc      'emcc -v'
chk sdl2      'pkg-config --exists sdl2 || test -d /opt/homebrew/opt/sdl2'
chk cmake     'cmake --version'
chk capstone  '.venv/bin/python -c "import capstone,pefile,unicorn"'
chk krn       'krn --help'
if [ ${#miss[@]} -eq 0 ]; then echo "tools: all present"; else echo "tools: missing ${miss[*]}"; exit 1; fi
