#!/bin/sh
# Build build/oracle/d3d8.dll, dinput.dll and dsound.dll (tools/oracle/*proxy.c) for the Wine reference run, using the
# Apple clang (x86 backend) and the lld/llvm-dlltool shipped with emscripten (its clang is wasm-only). One summary line; details in logs/build_proxy.log.
cd "$(dirname "$0")/../.." || exit 1
LLVM=$(dirname "$(readlink -f "$(command -v emcc)")")/../libexec/llvm/bin
[ -x "$LLVM/lld" ] || {
  echo "build_proxy: FAIL (no lld at $LLVM)"
  exit 1
}
OUT=build/oracle
mkdir -p "$OUT" logs
LOG=logs/build_proxy.log
printf 'LIBRARY kernel32.dll\nEXPORTS\nLoadLibraryA@4\nGetProcAddress@8\nGetSystemDirectoryA@8\nVirtualProtect@16\nOutputDebugStringA@4\nGetEnvironmentVariableA@12\nCreateFileA@28\nReadFile@20\nWriteFile@20\nCloseHandle@4\nGetModuleHandleA@4\n' >"$OUT/kernel32.def"
printf 'LIBRARY user32.dll\nEXPORTS\nShowWindow@8\nPostMessageA@16\n' >"$OUT/user32.def"
{
  "$LLVM/llvm-dlltool" -m i386 -k -d "$OUT/kernel32.def" -l "$OUT/kernel32.lib" &&
    "$LLVM/llvm-dlltool" -m i386 -k -d "$OUT/user32.def" -l "$OUT/user32.lib" &&
    /usr/bin/clang -target i686-pc-windows-msvc -O2 -fno-stack-protector -fno-builtin -c \
      tools/oracle/d3d8proxy.c -o "$OUT/d3d8proxy.obj" &&
    "$LLVM/lld" -flavor link /dll /noentry /nodefaultlib /machine:x86 \
      /export:Direct3DCreate8=_Direct3DCreate8@4 /out:"$OUT/d3d8.dll" \
      "$OUT/d3d8proxy.obj" "$OUT/kernel32.lib" "$OUT/user32.lib" &&
    /usr/bin/clang -target i686-pc-windows-msvc -O2 -fno-stack-protector -fno-builtin -c \
      tools/oracle/dinputproxy.c -o "$OUT/dinputproxy.obj" &&
    "$LLVM/lld" -flavor link /dll /noentry /nodefaultlib /machine:x86 \
      /export:DirectInputCreateA=_DirectInputCreateA@16 /out:"$OUT/dinput.dll" \
      "$OUT/dinputproxy.obj" "$OUT/kernel32.lib" &&
    /usr/bin/clang -target i686-pc-windows-msvc -O2 -fno-stack-protector -fno-builtin -c \
      tools/oracle/dsoundproxy.c -o "$OUT/dsoundproxy.obj" &&
    "$LLVM/lld" -flavor link /dll /noentry /nodefaultlib /machine:x86 \
      /export:DirectSoundCreate=_DirectSoundCreate@12,@1 /export:DirectSoundEnumerateA=_DirectSoundEnumerateA@8,@6 \
      /out:"$OUT/dsound.dll" "$OUT/dsoundproxy.obj" "$OUT/kernel32.lib"
} >"$LOG" 2>&1 || {
  echo "build_proxy: FAIL ($(tail -1 $LOG | cut -c1-140))"
  exit 1
}
echo "build_proxy: ok -> $OUT/d3d8.dll $OUT/dinput.dll $OUT/dsound.dll"
