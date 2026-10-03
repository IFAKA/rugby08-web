# Prior art: what already exists, and what we reuse (researched 2026-10-03)

Rule: before writing a subsystem, check this table. "Adopt" = vendor into third_party/ and wire up;
"Reference" = read its source for semantics, don't vendor; "Skip" = evaluated, not useful here.

## Facts about our binary that decide what we need
- Imports only system DLLs: KERNEL32, USER32, GDI32, ADVAPI32, WINMM, d3d8, DDRAW, DINPUT, DSOUND.
- **VP6 video decoder is statically linked** (`VP6_CODEC_INTERNAL::...`), and so is **EA-XA audio**
  (`CEAXABLKDec`, `SCHl` streams). The lifted code decodes them, so ffmpeg/vgmstream are NOT needed at
  runtime. They're useful only as offline references (to check decoded frames/audio).
- **D3DX8 is statically linked**, including the *D3DX8 Shader Assembler 0.91*: the game assembles
  vertex shaders from text at runtime and hands `vs_1_1` bytecode to `CreateVertexShader`.
  `DisableD3DXPSGP` exists, so D3DX picks SSE/3DNow! math paths by CPUID. Reporting a plain CPU
  keeps it on the x87 path.

## Recompilation toolchains
| Project | What | Verdict |
|---|---|---|
| [pcrecomp](https://github.com/sp00nznet/pcrecomp) | x86 PE → C toolkit (lift, catalog, difftest, Ghidra/RTTI) | **In use** (third_party/pcrecomp) |
| [sp00nznet/rol](https://github.com/sp00nznet/rol) | pcrecomp on Rise of Legends (MSVC 7.1, 2006, 25k fns): closest sibling binary | Reference: vtable/data-scan discovery. It keeps real DLLs on Windows (d3d9, dsound), so its runtime isn't portable to us |
| sp00nznet/gta, hover, blakestone | other pcrecomp titles | Reference only; Windows-native hosts |
| [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) / burnout3 | Xbox D3D8 HLE → D3D11; GL backend is a "first cut" | In use for ICALL ideas; its D3D8 layer is Xbox-flavoured and D3D11-first, so we don't adopt it |
| [Theseus](https://github.com/evmar/theseus) ([blog](https://neugierig.org/software/blog/2026/04/theseus.html), [wasm](https://neugierig.org/software/blog/2026/05/theseus-wasm.html)) | win32 x86 → Rust static translator, has a wasm target | Reference: browser lessons (below) |
| [DirectWebGPU](https://github.com/evangit2/DirectWebGPU) | Theseus + WineD3D-derived D3D8/9 → WebGPU, shaders via vkd3d-shader → SPIR-V → Naga → WGSL | **Closest prior art to the whole project.** Only one demo verified. Reference its `docs/wined3d-feasibility.md` and shader pipeline; it's Rust, so we don't adopt the code |
| [retrowin32](https://github.com/evmar/retrowin32) | win32 emulator in the browser (Rust) | Reference for Win32 API semantics |
| [Boxedwine](https://github.com/danoon2/Boxedwine) | Wine + CPU emulator, runs in browser | Skip (emulation; too slow for a 2007 3D game). Fallback only |
| N64Recomp / XenonRecomp / ReXGlue / PSXRecomp | console recompilers; PSXRecomp ships wasm | Reference: wasm-first build practice |

## Graphics (D3D8 → WebGL2)
| Project | What | Verdict |
|---|---|---|
| [d3d9-webgl](https://github.com/LostMyCode/d3d9-webgl) | D3D9 → WebGL2 | **In use, but limited**: fixed-function only (reports VS version 0), 3 point lights max, **no directional/spot lights**, stages 0–1 only, **no multi-stream**, no EXP fog. Expect to extend it or replace pieces |
| [MojoShader](https://github.com/icculus/mojoshader) (zlib) | D3D SM1–3 bytecode → GLSL / GLSL ES; FNA ships it on WebGL2 via emscripten | **Adopt** for `vs_1_1` (and `ps_1_x` if used). Includes GL glue (`mojoshader_opengl.c`) |
| [vkd3d-shader](https://gitlab.winehq.org/wine/vkd3d) (LGPL) | d3dbc SM1–3 parser; experimental GLSL target | Alternative to MojoShader. DirectWebGPU runs it in wasm |
| [d3d8to9](https://github.com/crosire/d3d8to9) | D3D8 → D3D9 | In use for interface layout. **Port its vertex-declaration conversion**. Its shader path needs D3DX (Windows-only) |
| WineD3D (`dlls/d3d8`, `dlls/wined3d`) | complete D3D8 semantics, FFP-in-GLSL generator | **Reference** for exact D3D8 behaviour (FFP lighting, texture-stage ops, caps). Too entangled to compile |
| [DXVK d3d8 (ex-D8VK)](https://github.com/doitsujin/dxvk) | D3D8 → D3D9 → Vulkan | Reference for D3D8 quirks; Vulkan isn't available in browsers |
| FNA3D (OpenGL ES3 renderer) | D3D9-style renderer + MojoShader on WebGL2 | Reference for wiring MojoShader output (attribute binding, constants) |

## Audio / input / Win32
| Project | What | Verdict |
|---|---|---|
| [dsoal](https://github.com/kcat/dsoal) | DirectSound (incl. 3D/EAX) → OpenAL | Reference for DirectSound buffer/notify semantics. Emscripten ships an OpenAL implementation, so adopting it is an option |
| Wine `dlls/dsound`, `dlls/dinput`, `kernel32`, `user32` | full implementations | Reference for semantics (we vendor only `.spec` files today) |
| [InputFusion](https://github.com/xan105/InputFusion) | DInput/XInput over SDL3 | Reference for gamepads (native.input) |

## Game-specific (EA Canada EAGL engine)
| Resource | Use |
|---|---|
| [EARugbyWidescreenFix](https://github.com/retrobone/EARugbyWidescreenFix) | Source with game addresses (aspect/HUD). Gives names for functions |
| [Rugby 08 data.gob file list](https://www.therugbyforum.com/threads/ea-rugby-08-file-list-for-modders.47550/), [OTools](https://www.therugbyforum.com/threads/otools-for-editing-o-and-fsh-files.47704/), [EA-Graphics-Manager](https://github.com/bartlomiejduda/EA-Graphics-Manager), [BIG format](https://rewiki.miraheze.org/wiki/EA_BIG_BIGF_Archive) | Asset format knowledge (.gob/.big/.fsh/.o) to check file I/O and textures |
| [EA formats wiki](https://wiki.multimedia.cx/index.php/Electronic_Arts_Formats), vgmstream | Offline reference decoders for checking VP6/EA-XA output |
| Ghidra FunctionID (VS2003 fidbs ship with Ghidra) | Name statically linked CRT/STL; D3DX8 needs a custom fidb (d3dx8.lib from DX8 SDK) |

## Browser lessons (Theseus-wasm, FNA, DirectWebGPU)
- Run the game on a **worker thread** so it can block (message loop, Sleep, waits). In emscripten
  that's `-sPROXY_TO_PTHREAD` + `-sOFFSCREENCANVAS_SUPPORT`/`OFFSCREEN_FRAMEBUFFER`, not asyncify.
- Keep host APIs behind one platform layer (SDL native / web). We already do this (plat.h).
- Per-draw constant upload and serialization dominated DirectWebGPU's cost (10 FPS). Batch them.

## Decisions this implies
1. Shaders: add MojoShader; `CreateVertexShader` → MojoShader GLSL ES → a GL program in our bridge.
   Done for translation (third_party/mojoshader submodule, runtime/d3d8/d3d8_vshader.cpp; GLSL dumps in
   logs/shaders/). Drawing with it is the next step.
   Don't fake the fixed-function fallback.
2. Extend d3d9-webgl (or take its fixed-function shader into our bridge) for directional lights,
   more stages and multi-stream, checking WineD3D's FFP for semantics.
3. native.video / native.audio: no ffmpeg. Video comes from lifted VP6 → textures. Audio comes from
   DirectSound buffers → SDL/OpenAL, following dsoal semantics.
4. CPUID: report no SSE/3DNow! so D3DX8 uses x87 paths (less lifted SIMD surface, simpler difftest).
   Already the case: runtime/include/recomp/recomp_types.h CPUID_impl models a Pentium Pro (no MMX/SSE).
