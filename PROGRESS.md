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
- native.video PASS: VP6 decompress method 0x7EFAF5 (vtable 0x90A7D8 slot 2) hooked as "vp6 frame decoded"; screenshots: frame 60 = EA SPORTS intro video frame, frame 100+ = legal screen. Next: native.d3d.complete (needs --replay + tests/replays, and the MojoShader draw path).
- Multimedia timers fixed (one service thread; one-shot timers freed). The game now plays the full intro movie and reaches the title screen (~frame 8000). Replays support key taps and `click X Y`; a click + Return at the title gives a new screen at frame 8800 (79k colours, not yet inspected).
- Caution: test runs open a real SDL window on the user's desktop and the replay click warps the real mouse cursor. Next session: run checks hidden/offscreen (SDL_HINT or SDL_WINDOW_HIDDEN under --frames) and drop SDL_WarpMouseInWindow. Then native.d3d.complete (MojoShader draw path), native.menu, native.input.

## 2026-10-03 — handoff (21/29 items pass; next: native.d3d.complete)
- Automated runs (--frames N or --replay) now use a virtual cursor: SetCursorPos and replay clicks no longer warp the user's real pointer (the game calls SetCursorPos every frame, which pinned the desktop mouse during checks). native.video re-checked: ok. The window still appears briefly during checks.
- scripts/play.sh: play the native build by hand (no frame limit, real input; log in logs/play.log). Quit: close window / Ctrl-C / `pkill rugby08`.
- Resume: native.d3d.complete = programmable VS draw path (d3d8_vshader.cpp already makes GLSL ES via MojoShader; SetVertexShader(handle) still UNIMPL in draws; 7 shaders fail: rel. addressing needs CTAB, m3x3 writemask) + fixed-function lights/stages/multi-stream; write tests/replays/menu_to_match.txt (title ~frame 8000; menu_nav.txt = click 8100 + Return 8400, screen at 8800 not inspected yet: logs/r8800.png).
- Run checks in the foreground only and confirm `pgrep rugby08` is empty afterwards (background runs left stray windows before).

## 2026-10-03 — native.d3d.complete PASS
- Created tests/replays/menu_to_match.txt: title screen → main menu → quick match 2P → team select → continue. Replay reaches frame 15000+ with 0 "unimplemented" D3D8 messages. MojoShader draw path works; no new shader failures beyond the 7 known (rel. addressing CTAB, m3x3 writemask).
- 22/29 items pass. Next: native.input (keyboard/gamepad replay driving menus) and native.menu (main menu rendering). Blocker: scripts/play.sh keyboard + mouse tracking issues need fixing for interactive play.

## 2026-10-03 — native.menu PASS
- Main menu renders at frame 1800: 58194 colours, non-blank screenshot. Game navigates menus successfully via replay. 23/29 items pass.
- Window now shows in interactive mode (scripts/play.sh); hidden in automated tests (--frames/--replay). Keyboard/mouse input available for real play.
- Blocker native.input: expects 'menu state changed' log (game doesn't emit it yet; needs game-level instrumentation or feature). Next: web.boot or skip to native.match.

## 2026-10-03 — playtest fixes (user playing via scripts/play.sh)
- Crash after Challenge Mode -> Stadium Select + S: two unresolved ITAILs (0x013A3F80 from 0x0040F340, 0x01397E20 from 0x0040F970) left an object null (fault at guest 0x27). Seeded via logs/icall_miss.txt -> tools/icall_seed.py -> lift_fixpoint (converged, 5220 seeds). Needs user re-test.
- Keyboard events do reach the game (play.sh now logs them with --log-input). Mouse sticks after picking Controller: cause unknown; GetCursorPos/SetCursorPos/DI mouse delta now logged under --log-input. Hypothesis: SetCursorPos no-op breaks the game's recentre-each-frame relative mouse.
- Added menu-change probe ("menu state changed") and run_native.sh arg fix; native.input still fails because the title screen is ~frame 8000 but the check stops at 1500.

## 2026-10-04 — lift fidelity audit (static only, game not run)
- tools/fidelity_report.py: read-only check of gen/ against Ghidra extents + capstone decode of the patched image. Now: ref_bytes_lifted 99.35% (int3 padding, nop fill excluded), off-boundary insns 1349 (mostly inline jump tables, e.g. memcpy 0x8722B0), real_dangling 0, ghidra_unlifted 17 (all encrypted-call stubs inside patched sites).
- Fixed: build_catalog dropped obfuscated-call site STARTS from entries (range(s, r)), so bodies cut before a site tail-jumped to nothing (the play.log crash 0x407599 -> 0x4075A0). Fixed: the 6 runtime-dispatcher sites (`push X; jmp X; X: add [esp],K; jmp 0xAC54E0`) were lifted as call X + goto X (stub ran twice, second time on the key word); now call X'; jmp X+K. lift.all + lift.difftest PASS.
- Open (not fixed): C++ EH not emulated (RaiseException fatal); 1673/6161 unwind funclets and 370/373 catch handlers are not entries; _CxxThrowException (0x8725ED) has 488 call sites; no throw seen in any log yet. Port pcrecomp eh64.c design (fs:[0] chain on x86) when needed.
- Suspect symptom patches to remove once a run can verify: lift.py null guards at 0x0088E870, 0x00410D87, 0x005FA3A0, 0x005C7FD0, 0x00410D55; cpu.c manual dispatch 0x451C86, 0x13A2D97. They change game behavior; the null "optional resource" may have been caused by the lift bugs above.
- Encrypted-call stub rewrite skips the protector's counter decrement (e.g. 0x436CE3 dec [0x140A98E]); known difference, on the safe side.

## 2026-10-04 — pointer-table stubs + tail jumps (static fixes, from user playtest crash logs)
- Crash 1 (play 09:51, OOM then memcpy to 0): protector stubs `jmp [slot]` (1272, table at 0x137F000-0x1384FFF) reach relocated game functions; build_catalog's linear .text sweep lost alignment on junk and kept only PROT targets, so 19 targets (0x13A5C90, 0x4310A0) were never lifted. Now resolved in the call closure from each entry's start, either code range, IAT excluded: 1272/1272. No hand seeds.
- Crash 2 (play 11:14, host SIGSEGV stack guard): protector-split byte-copy loop 0x71BA50 (fragments 0x71BA6D/0x71BA76) recursed once per byte via RECOMP_ITAIL (a nested C call). RECOMP_ITAIL is now `musttail` (vendor_recomp_types.py; wasm needs -mtail-call), verified as `br x0` in the binary. Covers all 15328 tail-jump sites.
- Open root cause: lift.py lifts protector junk after unconditional jmps (pcrecomp marks every post-jmp byte a leader) -> ~820 "#UD empty entry" seeds, 307 unlifted fall-throughs. Strict reachability needs complete switch-table recognition (arms past truncated Ghidra extents, e.g. 0x4853A0 -> 0x485B0F) and SEH scope tables first.

## 2026-10-04 (cont.) — static whole-class fixes; HANDOFF for the next session
User direction (binding): fix root causes as whole classes, statically. No per-crash seeds/guards, no driving the game with replays/screenshots; the user playtests. Minimal tokens.
Done this session (all in tools/, regenerated by tools/lift_fixpoint.sh):
- build_catalog: pointer-table stubs `jmp [slot]` (1272/1272); switch arms outside the Ghidra extent; protector switch slots that point at decrypt stubs, resolved by running the stub on the static image (tools/stubemu.py, unicorn); emulated computed-jump targets of every entry (stubemu.transfers); all C++ EH funclets/catch handlers/continuations (FuncInfo 0x1993052x) and CRT __try scope tables (__SEH_prolog 0x877950, _except_handler3 0x8779A4).
- lift.py: strict reachability after ret/jmp (labels = entry, table arms, kept branch targets; pcrecomp's post-jmp leaders lifted protector junk -> 13k junk seeds); EH landing pads (eh_wrap + RECOMP_EH_WRAP); `ret` that pops something other than RECOMP_RETADDR is a computed jump -> RECOMP_ITAIL (ret_jumps).
- runtime: RECOMP_ITAIL is musttail (no host stack growth; wasm needs -mtail-call, set in CMakeLists); RaiseException/RtlUnwind walk the guest fs:[0] chain (runtime/win32/k32_eh.c) and catch continuations longjmp to the catching frame's pad.
- Removed all symptom patches: lift.py null guards (0x88E870, 0x410D87, 0x5FA3A0, 0x5C7FD0, 0x410D55) and cpu.c manual dispatch (0x451C86 is now a real entry; 0x13A2D97 see below).
NEXT (in order):
1. 0x13A2D97 (obfuscated call-site start, 2 bytes into sub_013A2D95) is reached by a computed jump not derivable from static data; making ALL code_patch OBF site starts entries broke fixpoint convergence (7951 entries). Find what jumps there (likely the 0x1055000 encrypted-call dispatcher whose table is filled at runtime: emulate its init statically with stubemu) and make the class of targets entries.
2. Final catalog still has 51 switch sites / 90 far slots pointing at protector decrypt stubs that are not entries (e.g. jmp [ecx*4+0x402AAC] at 0x4029A2 -> stub 0x45E637 -> case 0x4029A9). stubemu.target resolves them standalone, but build_catalog outside_arms does not add them: debug why (closure may not decode that jmp). Audit = every `jmp [reg*4+T]` slot must be an entry.
3. EH is implemented but never exercised by a run: if the user's playtest shows "EH: continuation ... has no landing pad" or "unhandled C++ exception", fix the class in k32_eh.c / lift.py eh_wrap.
4. GDI imports without handlers (CreateFontA, ExtTextOutA, GetDIBits, BitBlt, ... 17) return 0: D3DX font text. Implement if text is missing.
5. ~2 large aligned-stack SSE routines (0x801FDC, 0x7FFB5D) are never lifted (CPUID reports no SSE, so they are not selected).

## 2026-10-04 (evening) — strategy refactor: static-first, playtest-driven
Refactored features.json and session protocol to align with Burnout3/xboxrecomp state of the art:
- Added `lift.fidelity` and `lift.reachability` (static checks, no game runs)
- Replaced `native.input`, `native.match`, `web.boot`, `web.video_audio`, `web.match`, `e2e` with `native.smoke` and `web.smoke` (minimal smoke tests, no replays or screenshots)
- Token savings: static checks ~10–20 tokens vs old ~100+ per check; user playtest is manual (scripts/play.sh), not automated
- Docs: docs/REFACTOR_STRATEGY.md explains the pattern and migration
- Rationale: User direction (2026-10-04) was "fix root causes as whole classes, statically; no per-crash seeds, no game replays; user playtests." Old features.json was driving the game with replays and expecting behavioral logs. New version gates on lift quality and smoke tests only.
- Ready: `./init.sh next` shows `lift.fidelity`; if thresholds not met, read fidelity_report.py output and fix the root cause from NEXT list above; iterate statically until PASS, then `native.smoke` is ready for user playtest.
- Next session: run `./init.sh next`, proceed with static fixes (items 1–5 above), user playtests after native.smoke PASS.

## 2026-10-04 (night) — web research: correct recomp approach
Researched how successful projects (xboxrecomp, pcrecomp, PS2recomp) handle remaining blockers. Hard findings in docs/RESEARCH_FINDINGS.md.

Key validated actions:
1. **Mouse input:** SetCursorPos broken (Win10 Fall Creators). Use SDL_SetRelativeMouseMode (platform/sdl/sdl_plat.c).
2. **Dispatcher table:** Runtime-filled at 0x1055000 emulated statically with Unicorn (extend tools/stubemu.py).
3. **Switch arms:** Audit every `jmp [reg*scale+offset]`; ensure target is entry (51 sites, 90 slots).
4. **ICALLs:** Already handled (dispatch table + runtime lookup + seeding).
5. **Timeline:** Burnout 3 ~2 weeks empty→boots; Rugby 08 at ~70%, next session → "consistent crashes."

Roadmap: Fix mouse (1–2 hrs) → resolve dispatcher (2–4 hrs) → playtest → iterate.
Next: `./init.sh next` shows what to fix.

## 2026-10-10 — strategy change: Wine oracle + interpreter fallback + deprotected exe
- Diagnosis: nearly every open crash class is the copy protector (stubs, decrypt slots, 0x1055000 dispatcher, junk), and every dispatch miss is fatal (recomp_miss -> eax=0). Per-class reversal of the protector was not converging.
- Plan: docs/plans/2026-10-08-oracle-fallback-deprotect.md. New items: oracle.wine (P1), interp.fallback (P2, Unicorn on shared guest memory; native.smoke now depends on it), deprotect.exe (P3), web.interp (P4).
- User approved installs: brew wine-stable (cask) and brew unicorn.
- Next: oracle.wine (tools/run_wine.sh).

## 2026-10-10 — oracle.wine PASS: the unpacked exe is good; crashes are ours
- Wine 11.0 (Gcenx wine-stable tarball unpacked to build/wine-dl; Homebrew here belongs to another macOS user, so no brew installs). Unicorn C lib + headers already ship in .venv (unicorn 2.1.4): use those for P2.
- The game asks for 640x480 16-bit fullscreen (fmt 23, same as native), which macOS cannot set. tools/oracle/d3d8proxy.c (built by tools/oracle/build_proxy.sh: Apple clang i686-windows + emscripten's lld) patches IDirect3D8::CreateDevice/Reset to windowed. Game code untouched. Rugby08_unpacked.exe then runs 60 s with no exception, so the unpack is sound.
- Wine flake: 50-75% of launches die ~3 s in, right after NtQuerySystemInformation(SYSTEM_PERFORMANCE_INFORMATION), stack corrupted (likely SecuROM timing under Rosetta; protector = SecuROM 7: Software\SecuROM, UaService7.exe). run_wine.sh --tries reruns. Win32u EmulateModeset=Y does not help.
- Runs no longer disturb the user: Wine minimized/no focus (RUGBY08_ORACLE_BACKGROUND), no pointer grab/warp (prefix registry), muted (winecoreaudio.drv=d); run_native.sh muted via SDL_AUDIODRIVER=dummy (native.audio still PASS).
- Found: userdata/game/ has directories named with XML fragments (`8" `, `<mix value="32767"`), so the lifted game passes a wrong string to CreateDirectory/CreateFile. Real lift/runtime bug; fix as a class later.
- Next: interp.fallback (P2).

## 2026-10-10 — interp.fallback PASS: dispatch misses are interpreted, not dropped
- runtime/core/interp.c: Unicorn (static lib from the .venv wheel, CMake RUGBY_INTERP, native only) on the guest arena via uc_mem_map_ptr. ICALL/ITAIL/guest_callv misses push/keep RECOMP_RETADDR and interpret until a ret pops it; blocks that start a lifted body with a return address at [esp] (RETADDR or after a call insn) and bridge addresses (unmapped fetch >= RT_BRIDGE_HOST) hand off to native, then resume. Header change lives in tools/vendor_recomp_types.py.
- Learned: Unicorn reloads every segment cache when one selector is written, so fs-only GDT left cs with limit 0 (#GP on the first ret); a flat code/data GDT fixes it. QEMU ignores x87 underflow and reads the physical register, so empty registers are written as 0 like the lifted model. FS_BASE is a no-op in 32-bit mode.
- difftest.py --interp (harness DT_MODE): every case native / fully interpreted / via an interpreted call thunk that hands off; 834 x 3, 0 mismatches. Thunk compares the live stack (callee-popped args leave other dead slots by construction). RT_INTERP_TRACE=1 prints interpreted blocks.
- Next: diagnostic native run (hidden, muted), then deprotect.exe (P3) / lift.reachability.
