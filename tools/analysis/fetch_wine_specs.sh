#!/usr/bin/env bash
# Download Wine's export .spec files (LGPL data: exact stdcall signatures of every Win32 export)
# for the 9 DLLs the game imports -> third_party/wine-specs/ (gitignored cache). One summary line.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
d=third_party/wine-specs
mkdir -p "$d" logs
base=https://raw.githubusercontent.com/wine-mirror/wine/master/dlls
ok=0
for dll in kernel32 kernelbase user32 gdi32 winmm advapi32 dsound dinput ddraw d3d8; do
  [ -s "$d/$dll.spec" ] && {
    ok=$((ok + 1))
    continue
  }
  curl -fsSL "$base/$dll/$dll.spec" -o "$d/$dll.spec" >>logs/fetch_wine_specs.log 2>&1 && ok=$((ok + 1))
done
echo "fetch_wine_specs: $ok/10 spec files in $d"
