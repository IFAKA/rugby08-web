# PROGRESS (append-only lab notes; read with `tail -40`)
<!-- BLOCKED notes go directly under this line -->

## 2026-09-29 — initializer session
- Plan: static recompilation via pcrecomp → clang/emscripten; D3D8 via d3d8to9 → d3d9-webgl; SDL2 for window/input/audio; ffmpeg VP6 for videos.
- Harness created: CLAUDE.md, features.json (checks gate `passes`), init.sh (status/next/check), scripts/run.sh (unattended driver).
- Submodules: third_party/{pcrecomp,xboxrecomp,d3d8to9,d3d9-webgl}. Tools installed (Ghidra 12.1.4 formula, not cask; analyzeHeadless in /opt/homebrew/opt/ghidra/libexec/support).
- Exe facts: imports d3d8, ddraw, dinput, dsound, winmm, user32, gdi32, kernel32, advapi32; uses CreateThread/events/CS/TLS/timeSetEvent; no D3D shader strings (likely fixed-function).
