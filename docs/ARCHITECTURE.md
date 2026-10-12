# Architecture (one page: what lives where, and the rules that keep it small)

Rugby 08 (PE32 x86, D3D8/DInput/DSound) runs natively on macOS and in the browser by **static
recompilation**: the exe's code is lifted to C once, compiled with clang/emscripten, and linked with a
runtime that plays Windows and DirectX for it. The game's content (Game Files) and the exe's data
sections are loaded at run time and never committed.

```
Rugby08_unpacked.exe ──tools/lift/──▶ gen/ (4.2M lines of C, generated, never edited or read whole)
                                         │ calls imports / COM methods through bridges
                                         ▼
runtime/  Win32 + D3D8 + DInput + DSound (target-neutral)   ──▶ render/webgl/  D3D9 subset on GL
   │ plat.h                                                         │ GLES3 / WebGL2, GL 4.1 core
   ▼                                                                ▼
platform/sdl (window, events, input, audio) + platform/native | platform/web
```

## Where things are
| path | what | read |
|---|---|---|
| `runtime/` | the Windows/DirectX the game sees (`README.md` per dir; ABI in `runtime/DESIGN.md`) | yes |
| `render/webgl/` | the renderer (grown from d3d9-webgl, ours) | yes |
| `platform/` | host differences: `sdl/` shared, `native/`, `web/` (+ `web/js/`) | yes |
| `tools/` | scripts by job: `lift/`, `gen/`, `check/`, `web/`, `analysis/`, `oracle/` | yes |
| `tests/replays/` | scripted input for automated runs | yes |
| `gen/`, `build/`, `analysis/`, `ghidra/`, `logs/` | generated, ignored | query only (`rg -m`, `sed -n`, `jq`) |
| `third_party/` | upstream submodules (pcrecomp, xboxrecomp, d3d8to9, mojoshader) | reference |

## Rules (`./init.sh structure`, `./init.sh lint`)
- Hand-written files stay **≤ 300 lines**; split by responsibility, one job per file. Exempt (grep them):
  `render/webgl/d3d9.h`, `runtime/include/recomp/recomp_types.h`, vendored `runtime/third_party/`.
- **`runtime/` is target-neutral**: `__EMSCRIPTEN__` only in `platform/` and `render/`.
- Every source directory has a short `README.md`.
- Debugging tools live in `runtime/debug/` and are armed only by env vars or `tools/hooks.json`.
- Formatting: clang-format (C/C++), ruff (Python), Prettier + ESLint (web JS), shfmt + shellcheck (sh).
- Never edit `gen/`: fix the lifter (`tools/lift/`) or the runtime instead.

## Proving a change
- Feature checks: `./init.sh check <id>` (features.json).
- Refactors and anything that must not change behaviour: `tools/check/regress.sh capture DIR --full` on the
  old and the new build, then `compare`. Runs are deterministic (`RT_DET=1`, fresh user dir): every pixel
  of 13 screenshots (intro, menu, a 3D match) and every guest state hash must match.
