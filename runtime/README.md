# runtime/ — the Windows/DirectX the game expects, on the host (native and web)

The recompiled game (`gen/`, never edited) calls Win32 and DirectX through import bridges; this
directory answers them. Design decisions: `DESIGN.md` (ABI, guest memory layout, bridges).

| dir | what |
|---|---|
| `include/` | interfaces: `rt.h` (guest memory, calls, logging), `plat.h` (host window/input/audio) |
| `core/` | boot, CPU state and dispatch, guest heap, threads, logging, crash trace, interpreter fallback, deterministic mode |
| `win32/` | kernel32, user32, winmm, advapi32, DirectInput, DirectSound (one area per file) |
| `d3d8/` | Direct3D 8 → the renderer (`render/webgl`, a D3D9 API on GL) |
| `debug/` | opt-in tools armed by env vars or `tools/hooks.json` (probes, coverage, shadow check, frame captures) |
| `third_party/` | dlmalloc (vendored, never linted) |

Rule: nothing here is target-specific (no `__EMSCRIPTEN__`): the host differences live in `platform/`.
