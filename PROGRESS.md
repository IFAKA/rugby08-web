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

## 2026-10-02 (night) — native.boot: root-cause hunt, paused
- Segfault chain: CRT startup 0x87555C -> _initterm 0x875BA8 -> static init 0x8B4A90 -> 0x593EF0 -> ctor 0x813CE0 -> 0x814690. There a virtual call `[edx+0x1C]` hits 0 because after `call sub_008146F0` the caller's ebp/esp are garbage (ebp=0, esp=0x10): a callee returns with the wrong ebp/esp. vtable data in memory is fine (checked), so it is NOT a data/vtable problem.
- Temporary check in RECOMP_CALL (ebp before/after, SEH helpers sub_00877950/sub_0087798B excluded as legit) flagged many callees whose ebp changes by 4-12 bytes (first: sub_00421550, sub_004506F0, sub_00495030, sub_004AAE00). Suspect: callees that return with a different esp/ebp than their caller expects (stack-discipline bug in lifted code, maybe the `ret N` / SEH epilog / push-push-ret rewrite), not SecuROM VM. Not yet confirmed.
- State left behind: build/native/rugby08 was built WITH that debug check; runtime/include/recomp/recomp_types.h is reverted (needs a rebuild to drop it). gen/recomp/recomp_0067.c has DBG rt_log edits (gen/ is ignored; `tools/lift_fixpoint.sh` regenerates it).
- Next: take one flagged callee (sub_00421550), compare its lifted epilog with the original bytes and run tools/difftest.py on it; then fix in tools/lift_ext.py. Also possible: search r/decomps for SecuROM 7 / HB Studios leads (user suggestion, not searched yet).

## 2026-10-03 — native.boot PASS
- Root causes were lifter-side, all in how protection-layer code transforms were decoded: (1) `push R; jmp T` fake calls (4,010 sites) lifted as a tail jump, so T's ret skipped R's epilog; now rewritten to `call T; jmp R` in tools/code_patch.py. (2) 156 of those have T == R: an encrypted call whose stub computes the callee from static data; code_patch emulates each stub with unicorn (150 resolve cleanly; 6 use a runtime-filled dispatcher table and stay as written).
- (3) Inline jump tables (CRT memcpy/memmove) were decoded as code, so arms like 0x8720D4 had no label; tools/jumptables.py adds a recursive descent with table slots as data for bodies that dispatch through a table. (4) Stale analysis/static_seeds.json from older lifts cut bodies mid-instruction; lift_fixpoint.sh now starts with no seeds. (5) __xc/__xi slots are now catalog seeds (ranges.INITTERM).
- tools/run_native.sh written (the native.boot check). difftest still 0 mismatches. lift.all/native.compile still pass.
- After WinMain: UNIMPL Reg*/SystemParametersInfoA/Direct3DCreate8/RegisterClassExA, then ITAIL miss 0x75E178 from 0x75E150 and a null ICALL -> segfault. Next item: native.window (and native.icall for the misses).

## 2026-10-03 — native.window PASS
- runtime/include/plat.h + platform/sdl/sdl_plat.c (SDL2, shared native/web): one host window, SDL events -> WM_* messages, VK/scan-code tables, cursor. runtime/win32/user32.c: classes, HWNDs, CREATESTRUCT/WM_NCCREATE/WM_CREATE, message queue (Peek/Get/Dispatch/Post/Send), DefWindowProc, keyboard, metrics. advapi32.c: no registry keys (lookups logged). runtime/d3d8/d3d8.c: Direct3DCreate8 traced, returns NULL for now.
- Also fixed in lift: jump-table end guard (tables placed after their arms were cut to one slot, e.g. switch at 0x75E150).
- Game creates "EAGL PC" 1352x878 then loops creating "EAGL PC Caps Retrieval" windows (63, never destroyed) because D3D/DirectDraw caps fail (Direct3DCreate8 NULL, DirectDrawCreate UNIMPL), then null ICALL from 0x766950 -> segfault. Message pump not exercised yet.
- Next: native.d3d.present (IDirect3D8 via d3d8to9 -> d3d9-webgl; DirectDrawCreate for caps).

## 2026-10-03 — prior art, MojoShader, DirectSound, protector stubs, MMX (native.icall in progress)
- docs/PRIOR_ART.md: survey of recompilation/graphics/audio prior art. Decisions: MojoShader for vs.1.1 (submodule added, runtime/d3d8/d3d8_vshader.cpp translates all game shaders to GLSL ES; 7 of 38 fail: relative addressing needs a CTAB, m3x3 writemask); no ffmpeg (VP6/EA-XA/D3DX8 are statically linked).
- Lift fixes: negated-index jump tables (memmove), tiny truncated Ghidra extents, protector call-dispatcher (0x1055006) table targets seeded statically (335), encrypted calls in the push/push/ret form now resolved by emulating the stub itself (84 sites; the site's own ret had been mistaken for the stub's -> callers lost ebp, which broke a TinyXML destructor), jump-form stubs and stubs reading the PE header.
- CPU model is now Pentium II + MMX: the VP6 decoder assumes MMX with no reachable C path. MMX_EXTRA ops added; difftest --grep '_mm\[' compares MMX bodies (~56 across 4 seeds, 0 mismatches).
- runtime/win32/dsound.c + platform/sdl/sdl_audio.c: DirectSound software mixer on SDL audio, --audio-capture WAV, tools/audio_stats.py. The game streams a 4-channel 44.1 kHz buffer; intro audio is captured (-21 dBFS).
- Native fault handler names guest address/function and dumps the trace ring. Game now reaches frame 60 and plays the intro, then exits: "FILE_loadpack - unable to unpack file e3606260..." (after an ITAIL miss in 0x415870; seeded, rerunning). Next: native.icall / native.fs (data.gob unpack), Shell Folders registry value.

## 2026-10-03 — native.icall, native.threads, native.fs, native.d3d.present, native.audio PASS
- Flags now cross calls and tail jumps (lift.py carry_flags): protector fragments split cmp/jcc, and /RTC's _RTC_CheckEsp tests the caller's flags. The game's own /RTC ESP checks (DebugBreak dumps the trace ring) then pinpointed the last stack imbalance.
- Dynamic code: EA AEMS sound modules (Audio/aems.big *.abk, ABKC banks) run from the heap. tools/aems_lift.py lifts all 17 banks (60 functions) at canonical addresses: function fixups are indices into the engine table at 0x9AF720, data fixups are rebased to aems_base[i]. runtime/core/aems.c finds each bank's base on first entry by masked byte matching (fe_sfx at 0x0237A7E0, same as the capture).
- Also: guest null page unmapped, RaiseException 0x406D1388 no-op, bare push/ret -> jmp, dispatcher thunk outside the code ranges, tools/img_stats.py.
- Game runs 900 frames: EA logo, intro audio, legal screen with text. Next: native.video (VP6 intro), native.d3d.complete (MojoShader draw path for programmable VS), native.input/menu.
