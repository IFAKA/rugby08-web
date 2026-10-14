# Rugby 08 in the browser — a static recompilation of EA Sports Rugby 08 (PC) to WebAssembly

**Play EA Sports Rugby 08 (2007, Windows PC) in a modern web browser or natively on macOS**, with the game's
own code: the original x86 executable is statically recompiled to C, compiled to **WebAssembly** with
Emscripten, and its **Direct3D 8** graphics, **DirectInput** controls and **DirectSound** audio are
re-implemented on **WebGL 2**, SDL2 and the Web Audio API. No emulator, no Wine, no virtual machine.

> **Bring your own copy.** This repository contains no game files, no executable and nothing built from
> them. You need your own copy of Rugby 08 for PC to build and run it. Rugby 08 and EA SPORTS are
> trademarks of Electronic Arts Inc.; this is an unofficial fan project, not affiliated with or endorsed
> by EA.

## Features
- The game itself: menus, Play Now matches, My Rugby, intro videos, crowd and commentary audio.
- Runs in Chrome, Edge and other Chromium browsers (WebAssembly threads, WebGL 2, JSPI) and natively on macOS
  (OpenGL 4.1 via SDL2).
- **Installable app (PWA)**: install it from the browser, launch it fullscreen, play offline after the first
  run; a new build updates the installed app by itself (no cache clearing).
- Keyboard, mouse (the menus' cursor follows the pointer) and game controllers; keys in
  [docs/CONTROLS.md](docs/CONTROLS.md).
- 60 fps frame pacing with `requestAnimationFrame`, and the game keeps running in a background tab.
- Deterministic mode (`RT_DET=1`: virtual clock and guest thread scheduler) for reproducible runs and
  regression checks.

## How it works
```
Rugby08 .exe ──tools/lift (Ghidra catalog + pcrecomp lift32)──▶ gen/ (≈4.2M lines of C, never committed)
                                     │ calls Windows imports and COM methods through bridges
                                     ▼
runtime/   Win32, Direct3D 8 → D3D9 subset, DirectInput, DirectSound, threads, files
render/webgl/   the D3D9 subset on WebGL 2 / OpenGL (shaders: the game's vs.1.1 → MojoShader → GLSL)
platform/  SDL2 (window, input, audio) + native (macOS) | web (Emscripten, JSPI, AudioWorklet)
```
- **Static recompilation**: every function of the executable is lifted to C ahead of time
  (`tools/lift/`), with x87 floating point, flags and jump tables handled; indirect calls go through a
  lookup table; a Unicorn-based interpreter catches anything not lifted (native builds).
- **Windows and DirectX in a runtime** (`runtime/`): the Win32 imports and the D3D8/DInput/DSound COM
  interfaces the game calls, in target-neutral C.
- **Game data streamed in the browser**: the 1.7 GB of game files are split into chunks and read lazily
  through a worker and a SharedArrayBuffer cache (`platform/web/js/gamefs.js`).
- More: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [runtime/DESIGN.md](runtime/DESIGN.md),
  [REVERSE.md](REVERSE.md) (facts about the executable), [docs/PRIOR_ART.md](docs/PRIOR_ART.md).

## Build it yourself
Requirements: macOS (Apple Silicon or Intel), CMake, clang, SDL2, Python 3, Emscripten (web), Ghidra and
OpenJDK 21 (the analysis step), and **your own Rugby 08 PC installation**: put its game files in
`Game Files/` and its executable, without the disc's copy protection, as `Rugby08_unpacked.exe` (neither
is provided here).

```sh
git clone https://github.com/IFAKA/rugby08-web.git && cd rugby08-web
tools/setup_submodules.sh        # upstream submodules + our patch series (third_party/patches)
./init.sh status                 # what passes, what is next (features.json)
tools/lift/lift_fixpoint.sh      # analyse and recompile the executable into gen/ (slow, once)
./init.sh build-native && scripts/play.sh    # play natively (macOS)
./init.sh build-web && ./init.sh serve 8080  # then open http://localhost:8080 (install it as an app)
```
`./init.sh` is the single entry point (`check <id>`, `check-all`, `lint`, `structure`); every check is
listed in [features.json](features.json).

## Status
35 of 37 tracked items pass: the game boots, plays complete matches with sound and video, natively and in
the browser. Open: removing the copy-protection stubs from the executable at build time
(`deprotect.exe`) and an interpreter fallback in the browser (`web.interp`). Progress log:
[PROGRESS.md](PROGRESS.md). An online multiplayer mode (lockstep, friends by party code) is in development
on a separate branch.

## Credits
Built on [pcrecomp](https://github.com/sp00nznet/pcrecomp) and
[xboxrecomp](https://github.com/sp00nznet/xboxrecomp) (static recompilation),
[d3d8to9](https://github.com/crosire/d3d8to9) (D3D8 → D3D9 rules),
[MojoShader](https://github.com/icculus/mojoshader) (shader translation),
[d3d9-webgl](https://github.com/LostMyCode/d3d9-webgl) (the renderer's starting point),
[SDL2](https://www.libsdl.org/), [Emscripten](https://emscripten.org/), [Ghidra](https://ghidra-sre.org/),
[Unicorn](https://www.unicorn-engine.org/) and dlmalloc.

**Keywords:** Rugby 08 PC, EA Sports Rugby 08 browser, play Rugby 08 online in browser, Rugby 08 WebAssembly,
static recompilation, x86 to WebAssembly, Direct3D 8 to WebGL, PC game port, retro sports game, PWA game.
