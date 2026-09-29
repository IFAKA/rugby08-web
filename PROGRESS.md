# PROGRESS (append-only lab notes; read with `tail -40`)
<!-- BLOCKED notes go directly under this line -->

## 2026-09-29 — initializer session
- Plan: static recompilation via pcrecomp → clang/emscripten; D3D8 via d3d8to9 → d3d9-webgl; SDL2 for window/input/audio; ffmpeg VP6 for videos.
- Harness created: CLAUDE.md, features.json (checks gate `passes`), init.sh (status/next/check), scripts/run.sh (unattended driver).
- Submodules: third_party/{pcrecomp,xboxrecomp,d3d8to9,d3d9-webgl}. Tools installed (Ghidra 12.1.4 formula, not cask; analyzeHeadless in /opt/homebrew/opt/ghidra/libexec/support).
- Exe facts: imports d3d8, ddraw, dinput, dsound, winmm, user32, gdi32, kernel32, advapi32; uses CreateThread/events/CS/TLS/timeSetEvent; no D3D shader strings (likely fixed-function).

## 2026-09-29 — session 2: analyze.pe
- User added .claude/settings.local.json (allow third_party tools, deny destructive/system cmds). The blocker is cleared.
- pcrecomp pe_analyze + extract_imports: 226 imports across 9 DLLs. analysis/imports.json and REVERSE.md written. analyze.pe PASS.
- DSOUND is imported by ordinal (1=DirectSoundCreate, 6=DirectSoundCaptureCreate).
- Next: analyze.disasm (disasm32, slow: hours per pcrecomp docs) running in the background, with analyze.ghidra in parallel.

## 2026-09-29 — session 2 (cont.): analysis done, lift.all
- Passing: analyze.ghidra (26,664 funcs), analyze.rtti (704 classes), analyze.disasm (54,031), analyze.score (recall 0.9941), lift.all.
- The lift catalog (`tools/build_catalog.py`) has 37,882 entries: Ghidra, RTTI, and "invented" starts that are real code (EH funclets and initterm initializers reached only through data). disasm32 split starts are excluded.
- `tools/lift.py` drives pcrecomp generate.py over our catalog, producing 146 MB of C in gen/recomp. `tools/lift_ext.py` subclasses lift32 so every instruction has Pentium Pro semantics (CPUID 0x619, no MMX/SSE/3DNow!): missing x87/BCD/etc. are implemented, extensions become #UD, privileged ops #GP, all through recomp_fault.
- Import signatures come from Wine .spec files (`tools/fetch_wine_specs.sh`, `tools/gen_imports.py`): all 226 are stdcall. Runtime core started: runtime/include/rt.h, runtime/core/cpu.c, runtime/core/cpu_ext.c, runtime/DESIGN.md.
- Next: lift.difftest, then native.compile.

## 2026-09-29 — lift.difftest PASS
- `tools/difftest.py` ports pcrecomp's difftest idea to whole game functions. Sampled leaf functions run in Unicorn and as lifted C from identical state, and GPRs, x87 depth mod 8, st0, and hashes of .data/scratch/stack/touched pages are compared. 10 seeds (~8,200 functions) show 0 mismatches.
- Real lifter bugs it found, all fixed in `tools/lift_ext.py` / `tools/code_patch.py`:
  - 933 obfuscated calls (`push R; push T; ret` + junk) are rewritten to `call T; jmp R` for the lifter. That also found 121 callees Ghidra missed.
  - Signed-imul UB widened addresses; generated code now always builds with `-fwrapv -fno-strict-aliasing`.
  - 8/16-bit div/idiv clobbered edx. #DE is now a logged soft fault.
  - fist out of range saturated instead of giving the x86 indefinite value.
  - fnstsw lacked TOP. _fpu_cmp started as "equal". fdivp/fdivrp skipped division by zero instead of giving Inf/NaN.
  - lift32 stubs for in/out now #GP, and MMX ops #UD (the modeled CPU has no MMX).
- x87 model limits (80-bit precision, 15-bit exponent, NaN bits, exception flags) are documented in runtime/DESIGN.md. The difftest inputs avoid them, and it excludes a case only when the reference shows the unmodeled state.
- Next: native.compile (CMake for gen/ + runtime/ with clang).
