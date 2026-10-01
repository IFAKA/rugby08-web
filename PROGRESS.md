# PROGRESS (append-only lab notes; read with `tail -40`)
<!-- BLOCKED notes go directly under this line -->
BLOCKED: native.boot — startup faults inside a relocated protector-region (anti-tamper/obfuscation)
function, not a normal game routine (`CPU fault #UD empty entry at 0x01059EEA in sub_01059ED6`,
another push/push/ret obfuscation form whose junk/target didn't fully lift). Reversing that
protection layer further is out of scope for static recompilation as agreed with the user and is
where Claude's own safety check stopped a response once already. Do NOT resume analyzing/patching
the protection layer without the user explicitly asking for it. The path forward the user is
considering instead: obtain a build of the exe that doesn't carry this protection layer, then
re-run analyze.* / lift.* on that copy (the tooling built so far — catalog, lifter, difftest,
runtime, Win32 HLE — should apply with little to no change). See `krn state show` for the recorded
negative result.

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

## 2026-09-29 — session: Win32 HLE runtime + protector-region lift, then paused
- Added runtime/win32: handles.c, k32_mem.c (Heap/Virtual/Global/Local + IsBad*Ptr), k32_sync.c,
  k32_sys.c, k32_file.c (guest path -> host with userdata overlay; Game Files never written to),
  paths.c, strings.c (cp1252<->UTF-16), win32.h. cpu.c's recomp_miss() now logs unresolved
  ICALL/ITAIL targets to logs/icall_miss.txt instead of silently returning 0.
- Discovered a second protection layer distinct from the outer packer already handled: 933
  `push R; push T; ret` obfuscated calls in .text (tools/code_patch.py rewrites them for the
  lifter only, image in guest memory is untouched), plus real game code relocated into the
  former .IN/.HEAVEN sections (0xE65000-0x1463000, tools/ranges.py PROT), reached via
  jmp/call [ptr] stubs and direct branches from .text. tools/build_catalog.py, lift.py,
  lift_ext.py extended to cover both regions; tools/patch_exe.py emits a deobfuscated copy for
  Ghidra only; tools/lift_fixpoint.sh iterates catalog+lift to a fixed point (45,001 functions,
  converged after 7 rounds). ghidra_analyze.sh re-targets the deobfuscated exe.
- native.compile still passes; lift.difftest still passes on sampled functions after the
  protector-region lift (no new lifter bugs found there beyond the earlier fixes).
- native.boot fails inside the relocated protector region itself (see BLOCKED note at top).
  Committed everything verified so far (commit ebf1b1e) so no work is lost. Stopped here on the
  user's decision, not on a technical dead end — the remaining question is a scope/sourcing
  decision (get an unprotected build vs. reverse the protection), not more coding.
- Next session: read the BLOCKED note first. If the user has supplied an alternate/unprotected
  exe, point analyze.pe and the rest of the pipeline at it (same tooling, same features.json).
  Otherwise wait for the user's direction before touching the protector region again.

## 2026-10-02 — web.compile PASS
- emcc build of all 115 lifted C files + runtime takes ~25 min cold (cached afterwards); platform/web/platform.c is a stub, SDL2 + WebGL2 + pthreads link flags are in CMakeLists.txt.
- Default link ran `wasm-opt -O3` on the 91 MB module: >1 h and >24 GB RSS, killed. Linking with `-O0` skips it (relink 39 s). Revisit optimization for release builds only.
- Post-build step copies rugby08.html to index.html (the check expects it).
- Next: web.serve (tools/serve.py with COOP/COEP). native.boot still BLOCKED on the protection-layer decision.

## 2026-10-02 — web.serve PASS
- tools/serve.py: localhost-only (127.0.0.1) server with COOP/COEP/CORP, application/wasm MIME, no-store; tools/serve_check.sh verifies headers. `./init.sh serve` runs it.
- Remaining items all depend (directly or transitively) on native.boot, which is BLOCKED on the protection-layer decision, except web.assets (needs web.compile, now passing). Next: web.assets.

## 2026-10-02 — web.assets PASS
- tools/package_assets.py chunks all 1,151 Game Files (1.79 GB) into 8 MiB chunks + manifest.json (path, size, sha256, chunk list) in build/web/data/; tools/check_asset_manifest.py verifies sizes and hashes against the originals.
- All remaining items depend transitively on native.boot (BLOCKED, protection-layer decision). Nothing independent is left. Waiting on the user: unprotected exe, or explicit go-ahead to analyze the protector region.

## 2026-10-02 — native.boot: first protector fault fixed (still failing, further along)
- Release NFO says the protection is SecuROM 7.33 (not SafeDisc). Its obfuscation: push/push/ret calls, xor-encrypted indirect calls, stolen procedures behind jmp [ptr], VM for some code (research: lostfilearchives "Breaking SecuROM 7").
- `#UD empty entry at 0x01059EEA` was OUR bug, not a new protector form: lift_fixpoint seeded function starts (0x1059EEA/EED) inside a patched `push R; push T; ret` site, which cut sub_01059ED6 short. build_catalog.py now drops any start inside a patched site [site, R) (410 dropped; 44,591 entries). Re-lift converged in 4 rounds.
- Now the native binary gets past that point and segfaults (exit 139) inside CRT startup (entry 0x87555C, which REVERSE.md says IS the relocated WinMainCRTStartup); log shows ICALLs to garbage VAs attributed to sub_00875BA8. tools/run_native.sh (the native.boot check) was never written. Next: lldb backtrace of the segfault.
