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

## 2026-10-10 — lift.fidelity PASS; lift.reachability 305 -> 54 (in progress)
- lift.fidelity check regex fixed to its description (ghidra_unlifted <= 20; old regex rejected 10-19). Passes.
- fidelity_report missed every EH function (`static void sub_X_eh` + `}void sub_X` wrapper): 305 -> 64. lift.py now writes the wrapper on its own line.
- build_catalog decodes_cleanly walks direct control flow: rejects candidates branching outside the image / into the PE header / zero fill; int3 ends a path. "Outside the code ranges" is NOT junk: SecuROM's runtime library lives in the dump's merged .data (0xC84600, 0xC85020 _chkstk; callers in PROT). Clean boundary: 0x9C0000-0xAC0000 all zero, code 0xAC0000-0xE65000.
- Remaining 54: 28 into SecuROM runtime (unlifted range), 25 zero-fill fall-throughs after noreturn calls, 1 junk. Next: add 0xAC0000-0xE65000 as a code range; lifter treats `00 00` targets as non-code.

## 2026-10-10 — lift.reachability PASS (305 -> 6), native.smoke PASS
- tools/ranges.py: third code range SECUROM 0xAC0000-0xE65000 (SecuROM's runtime library inside the old .ROT section; protector code calls it). Fixpoint now needs ~37 rounds (cap raised to 60); 57,199 entries.
- lift.py zero_fill_tails: a constant ITAIL into `00 00` (fall-through after a noreturn call) becomes recomp_fault("jump into zero fill").
- build_catalog: closure call targets also pass decodes_cleanly (junk entries started mid-instruction, e.g. 0x417F14).
- Rechecked after the full relift + rebuild: lift.fidelity, lift.difftest, interp.fallback, native.compile, native.menu, native.smoke all PASS; no dispatch misses to the menu.
- Next: web.smoke (deps pass) / web.interp; deprotect.exe is now mostly a speed/cleanliness item since misses are interpreted.

## 2026-10-10 — players render in matches (automated, no user playtest needed)
- Research (FFXI xi-on-anything, ps3recomp, XenosRecomp, DKC1Recomp): translate the game's shaders to the GPU language + replay suites with screenshots. Done here: D3D8 vs.1.x -> MojoShader GLSL run as GL programs (d3d9-webgl d3d9w_progvs_*), multi-stream inputs.
- Root causes of "empty pitch / cinematic POV": (1) D3DFVF_XYZB1 skinned meshes decoded as XYZRHW (shared bit 0x4) -> fvf_rhw/fvf_xyz/fvf_blend_weights + VERTEXBLEND with WORLDMATRIX(1); (2) stage-1 D3DTOP_BLENDTEXTUREALPHA (13) unimplemented -> kit decal texture multiplied players to black. Also typed (directional) lights and D3DMCS_COLOR1 material sources.
- Replays: menus accept with S (not Return); tests/replays/tutorial_match.txt reaches gameplay (~frame 11000, players at 14000). New replay commands: `<f> shot PATH`, `<f> press N`. Hidden runs now get WM_ACTIVATE* like a shown window. Frame-based timing is fragile across loading screens: prefer routes that wait on input.
- web.smoke check was vacuous (`|| exit 0`, missing script): now tools/web_check.sh (headless Playwright chromium). It FAILS (WinMain not seen in 180 s): next.
- Open from the user's playtest: black shards in the match stadium, mouse cursor frozen in in-match menus (keys work), pause does not work.

## 2026-10-10 — automated replay reaches a real match; Wine replay proxies
- tests/replays/play_now_match.txt: PLAY NOW -> Team Select (mouse moves the controller icon; arrow keys change teams) -> Stadium Select -> pre-match (All Blacks rendered) -> kickoff at ~21000. No black shards: they were the XYZB1 meshes drawn as XYZRHW screen-space triangles (fixed earlier).
- Wine 1:1 harness: tools/oracle/{d3d8,dinput,dsound}proxy.c play the same replay into the original (Present-counted frames, BMP shots, posted keys, DI state, GetAsyncKeyState/GetKeyState IAT hooks) and mute every sound buffer. run_wine.sh --replay FILE --shots DIR.
- BLOCKER for the Wine comparison: the original under Wine stalls on its loading screen after "teaminfo.xml ... Backup 3" (both with no audio device and with muted real audio), so frames never line up. Unknown cause (minimized window? Wine/Rosetta timing?).
- Open: in-match menu mouse cursor frozen (keys work); pause key unknown (Esc/P no effect in tutorial intro).
- Menus: MY RUGBY = Squad/Hall of Fame/Create Player/Game Settings/System Settings (Jukebox, Sound, Display)/Camera/Load-Save; no keyboard-binding screen, so the pause key is still unknown. In GAME SETTINGS, 6 Down taps 100 frames apart moved the cursor once (main menu: 4 of 4) -- possible input-timing issue.
- Web: the build had been failing silently since Oct 2 (emscripten cache in /opt/homebrew is not writable by this user; build/web held stale artefacts, so web.compile "passed"). init.sh build-web now sets EM_CACHE=build/emcache. web.smoke PASS for real: headless Chrome (fallback when the Playwright cache is absent) loads the page, 57,199 lifted functions, WinMain entered.
- Next for web: menu render in the browser (screenshot of the canvas via the headless browser), then web.interp.
- Web P4a in progress: -sJSPI + emscripten_sleep(0) at Present; first run hit 'SuspendError: trying to suspend JS frames' (JS setjmp invoke_* frames) -> -sSUPPORT_LONGJMP=wasm on all targets. Now aborts with 'missing Wasm export: setThrew': some object/lib (likely the SDL2 port in build/emcache) still uses JS longjmp. Next: rebuild ports with matching flags or find the stale object.

## 2026-10-10 — gameplay camera and a crash class fixed (lifter bugs)
- Camera stuck at ground level ("cinematic POV") during play: pcrecomp lift32.py lifted `fsincos` with sin/cos swapped (and `fprem1` as fmod). Fixed in third_party/pcrecomp (local commit). Broadcast camera now follows play (tests: build/match_play.txt shots 21000-28200). Note: difftest skips bodies with transcendental x87 ops, so these were never verified -- add a targeted x87 check.
- Crash after a try (FAULT guest 0x7 in sub_00432CFD): the protector's overlapping decodes (jmp into the middle of an instruction) were emitted in address order, so a fall-through ran the overlapping instruction the CPU never executes (`inc dword [eax]`). tools/lift.py overlap_fallthroughs/overlap_gotos: 110 sites in 29 files get an explicit goto to the instruction's real end.
- Open: mouse stops after assigning the controller to a team (user report); pause key; web: wasm exceptions rebuild done, untested.

## 2026-10-10 (late) — controls spec + open input/render bugs (handoff)
- Official behaviour (Rugby 08 PC readme, oldgamesdownload.com Rugby_08_Win_ReadMe_EN.txt): mouse navigates ALL menus; Team Select = move the device left/right; Esc opens the in-game pause menu; arrows run, Space sprint, S feed/throw/kick gauge, A/D pass, E/Q around ruck, W kick, Left Shift switch player, arrows aim placekicks.
- Pause bug: GEVENT_GAME_PAUSE = event 0x34 (handler FUN_0048F1A0 vtable 0x8D4AA4, gated by char this+0x23A2 >= 0); never fires in our sessions. WM_KEYDOWN handler 0x79E040 writes key table 0xA70B18 (nothing reads it); key-event ring 0xA70A58 feeds only getchar-style 0x890740. In-match keys come from the DI keyboard buffer 0xAA67E4 (Esc = 0xAA67E5); only reader found so far: 0x890AD6 `lea edx,[eax+0xaa67e4]` -- follow that function next.
- Mouse bug: after Team Select the game stops polling the DI mouse (logs: mouse2 polled until ~frame 11400, then never); cursor frozen. Same input system as pause -- likely same root cause.
- Render bugs: grey translucent band over the pitch in set pieces (unknown draw; use RT_TRACE_DRAWS_FRAME=N on a frame showing it); ball drawn as a black disc.
- Tools: RT_TRACE_DRAWS_FRAME=N (per-draw state), --log-input (DI mouse polls per device), replay `shot`/`press N`, tests/replays/play_now_match.txt reaches kickoff ~21000.
- Wine oracle still stalls on LOADING (not activation, not audio).
- Web: rebuilt with -sJSPI + -sSUPPORT_LONGJMP=wasm -fwasm-exceptions (finished, untested).

## 2026-10-11 — controls ground truth (docs/CONTROLS.md); pause/mouse root cause
- Authoritative keyboard map from the game: FUN_00821AF0 (device type 8) + reader FUN_0081FD80; EA manual (archive.org PS2 scan) for what each button does. START = Left Ctrl, Esc = logical 4, numpad = right stick. docs/CONTROLS.md.
- "Mouse frozen after Team Select" + "Esc doesn't pause": the device moved at Team Select becomes the match controller. Only the mouse can move its icon in our build, so the mouse becomes the pad (no cursor) and keyboard input is not the team's. Real bug: keyboard Left/Right reach the game (bits 0x40/0x80, FE messages 6/4) but never move the keyboard icon. Not found yet: the per-device stick -> icon code (input actions are data-driven, handler table at 0x90E7B0).
- Tools added: RT_COVERAGE="from-to:file" (functions entered per frame window, diff mouse vs keyboard), hooks "args:"/"ctl:" in tools/hooks.json, RT_TRACE_DRAWS_FRAME.
- Intermittent early crash (3 of ~9 runs, frame 1 or ~1740): sub_0088E870 null+4 in the VP6 stream ring reader while the movie stream restarts on another thread. Likely a guest-thread race; candidate fix: a global guest lock (one guest thread runs lifted code at a time), released around imports.
- FIXED (2026-10-11): runtime/win32/dinput.c EnumDevices now returns mouse then keyboard (Windows/Wine order). The game's controller list follows it and Team Select only offers entries 1..4, so the keyboard (was entry 0) could never take a team. Verified by replay: Left moves the keyboard icon to HOME, the penalty-options dialog then waits for the human, Esc opens GAME PAUSED with a live mouse pointer. native.menu/smoke/audio PASS. tests/replays/play_now_match.txt now assigns with the keyboard.

## 2026-10-11 — erratic running fixed: two lifter bugs in CRT math (sar on bytes, add carry)
- User report: in the tutorial the player moves erratically with the arrows. Reproduced by replay (tutorial SPRINTING drill, Right held): input was clean (stick x=1 every frame, new `stick:` hook), but the carrier flipped facing every few frames and never ran.
- New tool: shadow check (runtime/core/interp.c). `shadow:NAME` in tools/hooks.json, or RT_SHADOW_ALL=from-to for every function entered: the call tree runs first in Unicorn (writes logged and undone), then the lifted body runs and its registers/memory are compared (return-address trampoline 0xFFD00000). It pinpointed sub_006160F0 (stick vector -> heading) and the CRT atan2.
- Bug 1 (pcrecomp 46ee03b): `sar` on 8/16-bit operands was a logical shift. The CRT transcendental dispatcher classifies arguments with `fxam; fstsw; shl ch,1; sar ch,1; rol ch,1`, so atan2 took special-case handlers (atan2(0,2) returned 2). Every heading was wrong.
- Bug 2 (pcrecomp 04bcd78): `add` did not publish CF, so `_ftol`'s `add ecx,0x7fffffff; sbb eax,0` truncation fix-up read a stale carry: every (int)float cast rounded to nearest. (xadd too, tools/lift_ext.py.)
- Guard: features.json lift.crt_math (RT_SELFTEST_CRT: atan2/exp/_ftol of the game's own CRT vs libm at WinMain; 38 and 5 failures on the old lifters, 0 now). lift.reachability had regressed to 108 (overlap fall-throughs from 10-10 never got their fixpoint): tools/lift_fixpoint.sh rerun -> 1. difftest: kernel-space pages (>= 2 GiB) fault like page 0 (case not comparable).
- Still open from the shadow run: x87 push leak (g_fp_top grows ~16000/frame; sub_00778940/007789D0/00779BF0/007AD8E4/007BA0D6 net +16..+1616 pushes), sub_006694B0 esp/x87 mismatch, early VP6 race crash, grey band, black ball.
- Intermittent intro crash fixed (sub_0088E870, guest 0x4, ~half of runs): guest threads raced on EA's STRM ring. Lifted code does plain loads/stores; x86 is TSO, arm64 is not. runtime/core/cpu.c guest lock (default on, RT_BIGLOCK=0 off): one guest thread runs guest code at a time, released around every import and yielded every 4096 function entries (FIFO tickets). 4/8 crashes -> 0/8 and 0/12; tutorial replay 326 s -> 328 s. Watch item: a call-free spin loop on a flag would hang under it (then: yield on backward branches in the lifter).
- Shadow check fixes: x87 depth compared mod 8 (D3DX matrix multiply ends rows with ffree: TOP moves, as on hardware; the lifted top is an unwrapped count, not a leak); RT_SHADOW_ALL forces the guest lock (its undo is unsafe with other threads running: the sub_006694B0 "mismatch" and the abort came from that); mismatches list the lifted run's callees.

## 2026-10-11 (afternoon) — lighting root cause narrowed, pixel shaders (gated), movement checks
- Black ball / 4 shadows / shadows over players: per-model light rigs are never filled. sub_006D0AB0 copies a LightingInstance's "IrradLight" block (this+0x20, 0xA0 bytes) into the light state every draw; it is still the default identity. sub_0072B7C0 (registers each model's IrradLight::IrradBlock resources) never runs in our build and has no static caller or pointer anywhere in the image (protector-dispatched?). Next: find how the original reaches 0x72B7C0.
- Pixel shaders: CreatePixelShader/SetPixelShader(+Constant) translate ps.1.x via MojoShader and run in d3d9-webgl (linked with the programmable or an ES3 fixed-function vertex stage); gated by RT_PS_CAPS=1 (caps report ps.1.4). All 333 shaders compile; picture still wrong (grey pitch, black players) because the light data above is zero.
- MojoShader: CTAB-less relative constant addressing (whole constant file), m3x3 .xyzw dest narrowed, vs_1_1 `mov a0` = floor. All 62 vertex shaders translate; #59 still fails to link (vec4/float).
- Renderer: DXT1 punch-through alpha (black boxes around sprites), all texture address modes, general texture-stage combiner (stage 2 held off), D3D lighting model (8 lights, emissive, specular), 60 fps pacer (RT_FPS).
- Movement: input correct in all 8 directions; shadow check (now also CRT x87 _CI* helpers: ST0/ST1 passed in) finds no lifted/CPU mismatch while sprinting left. All user reports are in the tutorial SPRINTING drill, whose scripted goal is up-right: verify sprint steering in a PLAY NOW match before more work.

## 2026-10-11 (evening) — game clock drift (sprint feel), not yet playtested
- The game's clock (DAT_00a70c70, read by FUN_0079f130) is a counter incremented once per call of the one-shot TimeProc 0x8919D0, which re-arms itself with timeSetEvent(8-9 ms). dt = ticks / DAT_00a7ed18 (main loop FUN_00812c00, dt at app+0x78). Our timeSetEvent scheduled each re-arm from "now", so every late wakeup (guest lock, macOS timer slop) was lost game time and dt jittered.
- runtime/win32/winmm.c: a re-arm from inside the same proc is scheduled from the ideal due time (late ticks catch up; >250 ms behind resyncs). Built (build/native), not run. Check by playing: sprint turning, and `stick:` hook `clock dt` steady near 0.0167.
- Static review of lift32 x87 handlers (fpatan, fprem/fprem1, fscale, f2xm1, fyl2x, fistp, frndint) found no defect.
- Sprint steering workaround (not a root cause; needs playtest): runtime/win32/dinput.c reports Space released for 12 frames whenever the arrow keys change while Space is held, because steering with the sprint button up works in every direction. Proven so far: controller layer sees every direction while Space is held (logs/play-20261011-114039.log); lifted movement code matches x86 on the sprint turn (306/307); the game's sprint-turn gate is behind protector jmp stubs (e.g. FUN_0061AAE0 jmp [0x137CDDC]) and was not read. If it does not help, log speed/mode via FUN_006123E0.

## 2026-10-11 (evening) — shadows and black ball fixed at the root (user confirmed both)
- Shadows over players: d3d9-webgl reset two stencil ops whenever one was set and ignored the masks. The game marks players stencil 2 and draws flattened-mesh shadows where stencil < 1 (then 1), so the fix also stops the 4 light shadows stacking.
- Black ball: not IrradBlock (0x72B7C0 is a red herring). LightingInstance (0x6D0140) scales its IrradLight by a byte shadow map sampled under the object (0x6E4DF0). The map is rendered at stadium load into a 256x256 target and copied out with CopyRects (0x6E3549); our CopyRects copied the never-filled guest shadow, so the map was all zero. Now read back from GL.
- Next: pixel-shader path (RT_PS_CAPS=1) still renders players black; recheck now that the map is filled. Vertex shader #59 link failure.

## 2026-10-11 (night) — code review fixes (built, native.smoke/menu PASS, not playtested)
- SetGammaRamp was stored and never applied: now plat_set_gamma -> SDL_SetWindowGammaRamp (native only); trace line "SetGammaRamp" in play logs shows what the game sets.
- IDirect3DDevice9::Reset in d3d9-webgl was a no-op, so the game's Reset 1280x1024 (seen in play logs) left the backend at the CreateDevice size. It now updates logical size, window, back buffer surface, viewport, scissor; the D3D8 layer resets rt to the back buffer and resizes its wrapper.
- Dead paths (never hit in 50 play logs): CopyRects/LockRect on render targets. Left alone.
- Next: playtest brightness + options resolution change.

## 2026-10-11 (night 2) — shim fixes from code review (built; smoke/menu/audio PASS; not playtested)
- Handles: DuplicateHandle now makes a real second handle (h_dup, refcounted; CloseHandle closes a file's fd only with the last one); Create{Event,Mutex,Semaphore}A on an existing name return the shared object + ERROR_ALREADY_EXISTS (h_open_named).
- TlsFree clears the slot in every thread's TEB (thread.c keeps a TIB list); freeing an unallocated slot fails as on Windows.
- recomp_miss dedup is a hash set (was a 65k linear scan under a mutex per call).
- Caps/Num Lock toggle state reaches GetKeyState/GetKeyboardState; display mode list includes the desktop size.
- Sprint steering root cause (2026-10-11 evening): NOT game logic. The user's built-in MacBook keyboard works, a Bluetooth keyboard does not: keyboards cannot report Space + two arrows together (rollover/ghosting), so the game never gets the key. The 12-30 frame "sprint assist" workaround was based on the wrong diagnosis and is removed. dinput.c: Right Shift also reports Space (sprint) since modifiers never ghost. Probes kept (RT_PROBE, RT_PROBE_DUMP, RT_DUMP, RT_SHADOW_RANGE: env-gated, in log.c/interp.c). The winmm.c timer re-arm change stays (unverified effect).
## 2026-10-11 (late) — web build refreshed
- build-web failed on glMapBufferRange (d3d9-webgl upload_buffer NOOVERWRITE path, native-only); guarded with __EMSCRIPTEN__ (falls back to glBufferSubData). Rebuilt build/web, web.smoke PASS (reaches WinMain only; web.interp still false). Served locally on :8080 via ./init.sh serve. Not published: local-only constraint.

## 2026-10-11 (late night) — match slowdown (15-18 fps): vertex-buffer upload stalls (built, menus 60 fps, match not yet measured)
- New per-second `perf:` line in the play log (platform/sdl/sdl_plat.c): fps, avg/worst frame ms, swap ms, pacer wait. Menus/team select hold a flat 60; matches ran 15-18 fps; a few 30 fps stretches are the 30 fps intro video (half rate).
- `sample` of the main thread in a match: ~40% in glBufferSubData <- IDirect3DVertexBuffer8::Unlock (Apple GL-on-Metal flushes the queue and waits when a buffer the GPU may read is written). Fix: d3d8 layer passes D3DLOCK flags; d3d9-webgl upload_buffer orphans on whole/DISCARD locks, unsynchronized map on NOOVERWRITE; buffers <= 256 KB are re-sent whole as fresh storage. Submodule commit in third_party/d3d9-webgl.
- recomp_trace_enter ran the debug ring/probe/shadow/coverage work on every function entry (~20% of a 30 fps frame): now armed only by RT_PROBE*/RT_SHADOW_*/RT_COVERAGE or the first `shadow:` hook (runtime/core/log.c, interp.c).
- Replay scripts cannot reach a match (Team Select needs the controller icon placed on a side): measure matches by playing; next: read the match `perf:` lines, then 120 Hz = fixed 60 Hz simulation + interpolated render (game clock is a timeSetEvent tick counter; uncapping alone breaks sprint steering).

## 2026-10-11 (night 3) — web build plays a match in Brave (P4a done, user-verified)
- Root causes of "stuck at frame 1 / loading": (1) no game-data loader: platform/web/gamefs.js now mounts the 1,151 Game Files under /Game Files as lazy files; reads are synchronous POSIX calls on the main thread, so a fetch worker fills a SharedArrayBuffer chunk cache (32 x 8 MiB, from build/web/data) and the reader waits for its chunk. (2) Audio clock: the browser keeps AudioContext suspended until a gesture, so the DSound play cursor never moved ("buffer index not advancing, restoring" loop on the loading screen). sdl_audio.c on web: a mixer thread fills a shared ring that an AudioWorklet drains; wall-clock paced until the context runs, device-paced after.
- Console: the game's trace is no longer sent to the browser console (thousands of console.error lines); page ?log restores it (web_check.sh uses ?log). platform/web/shell.html is our shell. CMake now relinks when gamefs.js/shell.html change.
- Not verified headless: web_check stops at frame 120 (loading screen under swiftshader); user confirms the match runs in Brave. Audio glitchiness reported before the worklet change: recheck. Memory: Brave crashed once (Aw Snap, code 5) with devtools + ~36k console lines open.

## 2026-10-11 (late night) — web: renderer OOM fixed by splitting giant lifted functions
- "Aw, Snap" (code 5) / headless "V8 process OOM (Zone)": TurboFan needs ~1 KB of zone memory per wasm body byte; the lifted game has functions up to 850 KB (sub_0051D7E0 24k lines, sub_0084C6F0). Compilation hints (tools/wasm_hints.py) were ignored by Chrome; not used.
- tools/split_big.py (run by ./init.sh build-web -> build/web-gen, used by CMake for EMSCRIPTEN or -DRECOMP_SPLIT=ON): functions >3000 lines are cut at labels into ~1000-line parts passing control by SPJ (flush regs/flags, musttail, entry id); _fpu_cmp/_df/_itail_tgt become TLS globals; the jump-table dispatcher is its own part. Largest wasm function 857 KB -> 186 KB; peak TurboFan zone 291 MB -> 175 MB.
- Bug found by building the split natively (build/native-split, SIGSEGV in sub_00468440_eh): part 0 had no entry switch, so a jump back into it restarted the function. Fixed; native menu run clean. Native play build (gen/, build/native) is unchanged.
- web_check (headless, ?log): frame 3000 reached, 0 OOM, no JS errors. Not yet checked: a long match; audio quality.

## 2026-10-11 (night 3) — match frame rate 15 -> ~49 fps average (same replay), found by self-sampling
- Replay `tests/replays/play_now_match.txt` (hidden, muted, ~7.5 min) does reach a match (scrum scene); the game samples itself (`sample`, logs/sample-auto-N.txt when under RT_SLOW_FPS=24 fps for 3 s, 3 per run). `python3 -I /tmp/selft.py`-style self-time per main-thread frame is how the stalls were found.
- Stall 1: D3D vertex/index Unlock -> glBufferSubData (fixed earlier). Stall 2: DrawPrimitiveUP / DrawIndexedPrimitiveUP rewrote one shared GL buffer every draw (~1100 of 4100 main-thread samples): now orphaned first. Stall 3: the guest lock was released around every import incl. each D3D call, queueing the main thread behind busy guest threads (~50% of its time): D3D8 bridges keep the lock (Present/Reset/CreateDevice still release; cpu.c rt_host_bridge_keep_lock).
- Vsync: the vsync-blocking swap plus the 60 fps pacer quantized slow scenes to 30 fps: swap interval now 0 (RT_VSYNC=1 restores); same replay window 37.5 -> 48.7 fps average.
- winmm `timer:` line per second: the game's clock fires 100/s, 1.8 ms late avg: not the limiter.
- Open: dips to 30-44 fps with hitches up to 125-205 ms in some match scenes; kernel32 Sleep(0) spin in the game loop (hle_SleepEx ~10%); next sample them, then 120 Hz (fixed 60 Hz sim + interpolation).

## 2026-10-11 (night 4) — grey band / stadium overlap: D3D clip-space z not remapped (built, native.menu PASS, not playtested)
- Found by reading d3d9-webgl: gl_Position used the D3D projection as is. D3D clips z to [0,w], GL to [-w,w], so geometry nearer than the near plane (low POV camera, big stadium/pitch polygons) was not clipped and drew in front of everything. Fix: gl_Position.z = z*2 - w in the fixed-function VS and in the MojoShader-translated VS (MOJOSHADER_DEPTH_CLIPPING was never defined). Submodule d3d9-webgl edit, uncommitted.
- Next: user checks POV camera for the grey band; if it remains, trace draws (RT_TRACE_DRAWS_FRAME) on a frame that shows it.
- web: fps counter at top-left of the canvas (shell.html #fps, updated every 0.5 s from Module.rtFrames, which Present bumps). Follows the canvas into fullscreen. Headless check: intro video reads a steady 30 fps.

## 2026-10-12 — tidy branch: atomic layout, lint gates, dead code out (bit-exact vs master)
- Layout: render/webgl/ (the D3D9-on-GL renderer, was third_party/d3d9-webgl/d3d9.cpp), runtime/{core,debug,win32,d3d8}, platform/{sdl,native,web}, game/ (overrides), tools/{lift,gen,check,web,analysis,query,oracle}. Every hand-written file <= 300 lines (d3d9.h exempt), a README per directory, submodule patches in third_party/patches/. Docs: docs/ARCHITECTURE.md.
- Gates: `./init.sh lint` (clang-format, clang-tidy, ruff, shfmt, shellcheck, eslint, prettier) and `./init.sh structure` (repo.lint, repo.structure). Renderer API trimmed to what runtime/d3d8 calls (d3d9.h 988 -> 648 lines; caps/adapter/state-block/shader stubs gone).
- Referee: tools/check/regress.sh (RT_DET runs, fresh user dir) — screenshots, audio, log signature and the guest state hash every 60 frames identical to master's binary.
- Learned: the hash was only hermetic by luck. dlmalloc seeded its heap magic from time(0) (online's 289992c fix ported), and the arena must sit at a fixed address (host pointers in chunk headers). Runs started in different seconds never matched before.
- Next: merge online's changes onto this layout when the user asks.

## 2026-10-12 — online mode (branch `online`, worktree ../rugby08-online)
- Deterministic mode (runtime/core/det.c, RT_DET=1): one virtual clock for every guest-visible time source; one guest thread runs at a time, hand-overs decided by guest state + virtual time only; host input crosses only at Present; guest never sees the host window/desktop/cursor. Polling (32 clock reads <64 calls apart) and long stalls (>256 slices without Present) advance time; ordinary match frames are exactly 1/60 s.
- Netplay (runtime/core/netplay.c): lockstep from boot, inputs for frame f+delay, incremental state hash (one 1/120 slice per frame) exchanged every 120 frames, DESYNC stops both. Virtual DirectInput pads (Logitech DualAction per controltypes.csv; game reads sticks only, d-pad mapped to the stick). server/main.go: stdlib relay (rooms, lobby, COOP/COEP static, password). Web lobby in platform/web/shell.html.
- Checks: net.det_synctest, net.det_chaos, net.lockstep (1v1 into a match), net.desync_detect, net.web_lockstep all PASS. Browser vs native cross-play desyncs (different builds): online is browser-to-browser.
- Next: per-player camera (render-side view correction), live-input check in a real browser, deployment notes.
- 2026-10-12 (later): per-player camera (runtime/d3d8/d3d8_pov.cpp, key C): local view swap for fixed-function draws, clip correction for shader draws (registers 96..99, tools/patch_d3d9w.py). The visible match is fixed-function. Independence proven: net.pov_independent (player 2 on its own camera, 15000 frames of equal hashes). Live browser input: net.live_input (Playwright). docs/ONLINE.md: how to host. Server TLS flags. Regression: native.smoke/menu/audio, lift.crt_math, web.smoke PASS.
- Not done: browser-vs-native cross-play (desyncs: different builds), rejoin into a running match, spectators.

## 2026-10-12 — online: drop-in party inside the game (branch `online`)
- User test showed "Out of sync" on the legal screen. Two root causes: the guest saw this machine's mouse buttons/lock keys in det mode (a click skipped the legal screen on one side), and dlmalloc seeded its heap "magic" from time(0) (copies started in different seconds differed from frame 1). Both fixed; tests now click the canvas and start copies seconds apart.
- Redesign per user: no lobby page. The game boots straight in with its room open; friends join any time from the in-game Friends panel's invite link (server replays the room log; the newcomer catches up without drawing/sound), roster changes ride in the message stream, per-player input delay, room outlives its creator, desynced copies auto-rejoin, lost connection = play on offline. Web det mode now paced at 60 fps.
- New checks pass: net.dropin, net.handoff, net.web_dropin. Limit: joining cost grows with time played (native catch-up ~1.3x real time with drawing).

## 2026-10-12 — intro movie 7 fps in the browser: deterministic clock stood still in Sleep(0) wait loops
- Root cause: the movie player waits per frame in `while (!frame_due()) { tasks; SleepEx(0, TRUE); }` (sub_006BD92A -> sub_007ED528, EA's sound clock). In det mode Sleep(0) passed no time and the loop body is too long for the clock-read spin rule, so time crept by the stall rule only: 7 fps browser, ~14 native det, 25 native normal (the movie's own rate). The mixer only looked hot because it is the spin body.
- Fix: det_sleep_ms(0) — from the DET_SPIN-th Sleep(0) since the last Present, each passes DET_SPIN_NS. Pacers (frame_pace, web_pace) in det mode wait the virtual time each Present advanced (det_period_ms), else the fixed intro ran up to 2.4x fast. Native det: 91 movie frames at exactly 40.0 ms; browser ~23-25 fps, page 78% idle.
- Lifter speedups (any mode): `call` to a `jcc rel8; ret` leaf (MSVC /RTC _RTC_CheckEsp, 741 sites) lifted inline; MSVC 7.1 _ftol2 matched by bytes (5 copies, 204 sites) -> recomp_ftol2; RECOMP_ENTER fast path inline. Selftest compares recomp_ftol2 with the lifted body in all 4 rounding modes (836 cases, 0 wrong). tools/web_profile.py: headless Chrome CPU profile per lifted function.
- This worktree now lifts into its own gen/recomp (was a symlink into ../rugby08; renamed to gen/recomp.main-link).
- Checks after the change: native.video, native.menu, net.det_synctest, net.det_chaos, lift.crt_math PASS; net.*/web.* batch was still running at commit time. Next: menu transition dips (35 fps) and the "save roster?" dialog (18 fps) in det mode.

## 2026-10-12 online: crashes, controls, decision for host-authoritative
- Fixed browser crashes: split jump tables (MY RUGBY), unlifted CRT exp arms (scrum); checks web.split_check, web.crt_math, lift.crt_math (special args, misses).
- Online lockstep improvements: host keyboard+mouse in the input stream, code-based Join, rAF pacing, cheaper desync hash.
- Decision (user): online must be inside the game, everyone in their own game, shared matches, any device/network -> host-authoritative netcode. Plan: docs/plans/2026-10-12-online-host-authoritative.md, items on.* in features.json.
- Next: on.match_map.

## 2026-10-12 — online: host keyboard did nothing in menus / intro (fixed, user-verified in Brave)
- Root cause: in a session DirectInput lists the 4 virtual pads before the keyboard (rt_pads_first), so the keyboard is controller 5; the game only reads controllers 1-4 (FUN_0081A5A0(i, 4): movie skip loop sub_006BD870, menus, Team Select). The mouse is read separately, so it kept working.
- Fix: the host's keyboard also drives player 1's virtual pad (plat_local_pad, same mapping as friends: Enter/S accept, Esc Start, arrows), and still feeds the game's keyboard device. Replays are unaffected (automated runs skip plat_local_pad).
- shell.html: an Esc that releases pointer lock (the browser keeps the key) is handed to the game as an Esc tap.
- Verified: headless Chrome, Enter during inenpc.vp6 -> title screen. Found by native det RT_COVERAGE/RT_PROBE diff vs web (?env=RT_COVERAGE=a-b:/dev/stdout prints coverage to the console).

## 2026-10-12 — branch `online-ha` (worktree ../rugby08-ha): online merged onto the tidy layout
- Per user: master fast-forwarded to tidy (ec043f5), new branch online-ha from master, `online` merged in (one merge, conflicts resolved against the new layout). Ported by hand where tidy split files: sdl_plat.c -> sdl_window/keys/events + new sdl_desk.c (netplay keyboard+mouse) and sdl_pad.c (live pad); dinput/dsound/log hunks into their split files (dinput_state.c, dsound_mix.c, runtime/debug/selftest_crt.c, miss.c); frame pacing into platform/native/frame.c and platform/web/platform.c (det_period_ms now in det.c); tools/patch_d3d9w.py replaced by a direct edit of render/webgl/program_ff.cpp (VS constants 96..99); online's tools moved to tools/net/, tools/check/, tools/web/.
- Gates kept green: det.c hashing -> det_hash.c; netplay.c -> netplay.c + net_frame.c (step split into named helpers) + net_join.c; POV draw hooks -> d3d8_pov.cpp; target #ifdefs out of runtime/ (plat_frame_yield is a native no-op). `./init.sh lint` and `./init.sh structure` ok.
- Verified: merged lifter output byte-identical to online's gen/recomp (143 files). On the merge build: net.det_synctest, net.lockstep, net.pov_view, net.pov_independent, net.desync_detect, net.handoff, native.smoke, native.menu, lift.crt_math PASS.
- Learned: the auto-merge silently duplicated a function both sides added (handles.c can_acquire: the det commit is on both branches); the compiler caught it.
- Next: web build + web checks, regress compare against ../rugby08-base, then on.match_map on this branch.
- Web on the merge: clang-format had split `=>` in EM_JS JavaScript (build failed); fenced with clang-format off/on. web.compile/assets/serve/smoke/crt_math, net.web_lockstep, net.live_input, net.web_dropin PASS.
- regress compare vs ../rugby08-base (master): all 14 scenarios differ, as expected from online's intended det-mode changes (Sleep(0) passes virtual time, movie pacing); not yet shown equal to online's own binary.

## 2026-10-12 — on.match_map PASS (branch online-ha)
- runtime/core/match_state.c + include/match_state.h: ms_active/phase/clock/player/ball/score/controlled, RT_MS_LOG=N trace line; probes moved to runtime/debug/match_probe.c (RT_MS_SCAN/DUMP/SNAP).
- Verified on screenshots of the same run: HUD clock = decoded clock (01:47 at f14400, 29:28 at f29400), score 0-3 -> 0-10 matches, phases Free = open play, Formation = rucks/mauls/scrums, LineOut, GoalKick, Kickoff, Empty = cutscenes. Open-play clock 8.8 s per 60 frames (81/81). Points table settled: try 5, type 1 conversion 2, types 2/3 three.
- Check tools/net/match_state_check.sh (eval match_state_eval.py) PASS. Next: on.match_setup (setup bytes; two natives by different menu routes reach kickoff with identical setup).

## 2026-10-12 — on.match_setup PASS (branch online-ha)
- The whole match definition is the setup block (0x9B6830): rosters (+0..+0x18C0, written at Team Select confirm), extras, conditions/wind, half length +0x18EC, stadium +0x1902, difficulty +0x190C (Stadium Select commit FUN_004FCDD0). Found by single-setting diff runs (half length, difficulty) in det mode.
- Launch step FUN_004F3D10 (called by every mode's commit) hooked via tools/hooks.json "ms:launch" -> runtime/core/match_setup.c: records the setup (RT_MS_SETUP_OUT), replaces a friend's (RT_MS_SETUP_IN, later the network), RT_MS_AFTER_LAUNCH=K:PNG.
- Check tools/net/match_setup_check.sh: friend route tests/replays/friend_my_rugby.txt (MY RUGBY first, Portugal v Australia, Tbilisi) plays the host's match: setup/teams/stadium equal, picture diff 1.5 vs 27.0 for its own match.
- Learned: menus use W (Triangle) for Back; Esc/Backspace do nothing there. Next: on.match_puppet (friend applies host state every frame, local simulation suppressed).

## 2026-10-12 — on.match_puppet PASS (branch online-ha)
- Friend game plays the host's match: CRugby::Update logic skipped (game/overrides/match/rugby_update.c), host frame applied: actor heading/position + ball (FUN_0044B9E0/FUN_00620800) after presentation; poses via the game's replay branch (pose_player.c + replay_playing.c: FUN_004039B0 with the host frame as replay frame A); camera = host's 16 floats re-applied after FUN_006E8260 (camera_update.c, runtime/core/match_camera.c); score events via team stats + FUN_005CE320; kits = setup+0x191C (setup block is 0x1924 bytes).
- Tooling: tools/query/callgraph.py (static call graph from gen/recomp), RT_MS_POS lines + tools/net/match_compare.py (host vs friend by match time, incl. the friend's own recorder vs host frame), RT_MS_SHOT_T screenshots at match times, args: hooks print ret.
- Bugs found by measurement: recorder wrote an uninitialised frame B (headings 1e33 -> FUN_004AC5B0 angle loop hang); presentation tail passed dt to the getters; phase replay by id crashes (set-piece objects missing) -> not replicated.
- Check tools/net/match_puppet_check.sh: 16599 frames within 1 cm (players, ball), score 0-10 as host, picture diff 5.9 vs 32.4 (control). on.match_map / on.match_setup re-run: ok. Next: on.match_net (friends' pads to the host, host frames to friends over the server).

## 2026-10-12 — on.match_net PASS (branch online-ha)
- runtime/core/net_ha.c (--net-mode ha): host sends 'S' setup at launch and per recorded frame 'F' (96-byte head + raw key or the game's own replay record); friend sends 'P' pad every Present and 'K' for a key; friends' pads are host controllers 2..4. server/: relays S/F/K/P without logging, keeps last setup + key for newcomers (Go tests).
- The game's replay records decode in another game (RT_MS_STREAM_CHECK: pose blocks and ball identical over 3310 frames from one key; positions quantised to 1/52.5 m, headings to degrees): ~377 B/frame instead of 4.6 KB raw.
- Learned: once a friend plays, the host's match takes another course (a human side's scrum waits for its input), so test inputs on the friend are triggered by the host's phase (runtime/debug/net_ha_press.c), not by frame.
- Check tools/net/match_net_check.sh: 3799 friend frames = host frame shown within 3 cm, latency p95 0.9 frames, friend's pad moved its away player 18.8 m on the host (0.0 m before). Next: on.match_flow.

## 2026-10-12 — on.match_flow PASS (branch online-ha)
- runtime/core/net_ha_flow.c: friends follow the host into its match from any menu ('S' -> FUN_004F3D10), out of their intro (FUN_0047D860 when host frames arrive), back to their menus ('E' -> FUN_00576140); pause: friend's pause menu -> 'Z' -> host Esc -> 'U' to all; the pauser's pad drives the host menu.
- Learned: pause state is the FE screen id (0x71) not BackendState+0x2220 (a counter); only keyboard Esc pauses; a controller needs a side to drive menus; once a friend has a side the host's match waits for its set-piece answers (test input mashes Cross). Stale relays from another worktree held fixed ports: checks now pick a free port and fail if the relay does not start.
- Test tools: RT_MS_QUIT_AT / RT_MS_RELAUNCH (match_quit.c), RT_NET_HA_PAUSE / RT_NET_HA_MASH (net_ha_press.c).
- Check tools/net/match_flow_check.sh: 2 matches, friend joined both from its menus with the host's setup, frames within 3 cm, left both within 120 frames, its pause held the host's clock 93 frames, its Cross resumed. Next: on.ingame_ui.

## 2026-10-13 — on.ingame_ui: native PASS, web blocked by a browser freeze (branch online-ha)
- In-game Online screens in every game (no flag): F2 -> HOST A GAME (room code) / JOIN A GAME (type it), badge, LEAVE; runtime/core/ui_*.c draw an RGBA image (5x7 font) that render/webgl/overlay.cpp blends over the frame at Present; keys pressed while open never reach the game; the web page starts the game directly (HTML panel only behind ?panel / ?net=auto).
- The game enumerates controllers once at boot (a direct CPCControlMgr rebuild crashed), so all 4 virtual pads exist from boot by default (solo Team Select shows 3 idle pad icons); the reference replay checks pin RT_PADS=0 (the original controllers they were recorded with). Web audio pacer: a running AudioContext that drains nothing no longer freezes DirectSound's cursor.
- ingame_ui_check passed with native host + native friend + browser friend once; the recorded run failed: the browser game's main loop froze (frame 3240, intro movies) — the same freeze makes net.web_dropin fail, and it predates this work (bisect: fails at 15e8b14 too). Next: that freeze, then on.cross_platform (check written).

## 2026-10-13 — new plan: online party (docs/plans/2026-10-13-online-party.md)
- Agreed with the user: browser only, solo or party (code), like sports games' online friendlies; ONLINE in the game's own menus (pad, mouse, keyboard, touch), Team Select on everyone's screen with side captains, own camera/settings, pause for all, leave by closing the page, every multi-player mode, WebRTC + host input delay + friend prediction, no phantom controllers, page fills the window.
- features.json: on.ingame_ui / on.cross_platform / on.deploy_ready replaced by pt.freeze … pt.deploy (15 steps, browser checks); net.web_dropin waits on pt.freeze.
- Unverified freeze-fix ideas are in `git stash` ("freeze investigation (unverified)"). Next: pt.freeze.

## 2026-10-13 — pt.freeze PASS (branch online-ha)
- The test "freeze" was a fatal: before the first click the AudioContext is suspended and the pacer wrote the ring on the wall clock; the click started the worklet on seconds of backlog, the pacer waited for it to drain, DirectSound's cursor stood still and the game restored its device until "COM object table full". Fix: the worklet (sole writer of the read index) drops more than 2x the target latency (platform/web/js/audio.js); the pacer paces by the wall clock while the sink is stalled.
- Background tab: hidden pages paced by a worker's timer (platform/web/platform.c), not rAF/setTimeout (stopped / 1 Hz in hidden tabs). Headless Chrome never hides a page, so tools/net/web_soak.py gives the host's page what Chrome gives a hidden tab (document.hidden, visibilitychange, blur, no rAF, timers >= 1 s); negative control with the old pacing: hidden host 0.1 fps, friend stopped.
- Friend crash found by the soak: a friend at the title screen launched the host's match 15 times; the loading state's package unload then read garbage names (wasm OOB in stricmp). net_ha_flow.c launches only past the title and never while the Game has a queued state.
- Headless Chrome renders with the real GPU (Metal) by default: ~60 fps in a match vs 6 with SwiftShader; browser tools use web_route.browser(). tools/net/web_route.py drives menus by the traced screen id. Check web_soak.py: 10 min, two pages in a party match, host hidden 3 min at 59 fps. Next: pt.research (written in parallel: docs/PARTY_RESEARCH.md).

## 2026-10-13 — pt.research PASS (branch online-ha)
- docs/PARTY_RESEARCH.md (four parallel read-only Explore agents, static): Modes (front end = numbered screens + named callbacks in 4 tables; side arrays 0x9C8820/0x9C881C and locks; human-team flags per competition; launch/return via FE+0x3144C), Main menu item (screens are .rfe XML/binary loaded by 0x4EBAE0 → 0x5A06D0; main menu = screen 50 TextButtons; callbacks cdecl int cb(obj); keyboard screen 999 for the letter grid), Controller hot-plug (no re-enumeration; CControl+0x2580 "attached" drives Team Select icons and team building), Player movement (HumanMove 0x6BE760, speeds/accel tables, a kinematic prediction model).
- Corrected: vf7 does not call EnumDevices (INPUT_init 0x890B70 does, once). Next: pt.controllers (attached flag per pad slot, lowest free slot, keyboard moved past entry 4 when 4 pads).

## 2026-10-13 — pt.controllers PASS (branch online-ha)
- runtime/core/pad_attach.c: every Present the game's controller list follows the pad slots: CControl+0x2580 "attached" (what Team Select's icons/sides and team building read) per slot, off its side and inactive before unplugging; plugged devices kept in entries 1..4 (keyboard first, after 4 plugged pads). Slots: 0 this machine's game controller (plat_gamepad, also solo), 1..3 the host's party members (ui_lobby member slots). DIDC_ATTACHED at boot from the same rule; RT_PADS=N runs keep all N plugged and the boot order.
- Host: no more keyboard-as-pad duplicate (its pad 0 is its game controller); friends' keyboard+controller go to the host as before.
- Check tools/net/party_controllers_check.py: solo Team Select = M K P0- P1- P2- P3- (keyboard only); 3 friends join → 3 controllers; sides K:H P1:A P2:H P3:A; the leaver's controller goes, off its side. Native match_net/match_flow still pass.
- Seen: a friend's keys typed on the F2 overlay reach the host as pad input (changed the host's team): pt.menu must keep keys the party screens use out of the pad sent. Next: pt.webrtc.

## 2026-10-13 — pt.webrtc PASS (branch online-ha)
- platform/web/js/rtc.js: each friend's browser calls the host's (RTCPeerConnection, star); channels "rel" (ordered: S/E/Z/U/K), "frames" (reliable, unordered: F), "fast" (unreliable: P). A message goes peer to peer only when every peer it is for has its channels open, else over the relay, never both. server/signal.go relays offers/answers/candidates ({"t":"signal"}); server/stun.go answers STUN itself (-stun PORT; "joined" tells the page) — no third party.
- Frames carry a sequence number; runtime/core/net_ha_rx.c (split out of net_ha.c) puts them back in order in a window and ends a chain only when a frame stays missing (30 later ones), then asks for a key; 'E' carries the match's last sequence number so late frames of an ended match are ignored.
- Measured and changed from the plan: frames over an unreliable channel cost ~12 frames of stall per loss (records are deltas; a loss waits for a key round trip): 1208/2299 frames shown at 3 % loss. Frames now go reliable+unordered (a loss = one retransmission); pads stay unreliable.
- Check tools/net/party_webrtc_check.py: 2296/2296 game messages peer to peer, latency p50 29.1 / p95 38.6 ms vs relay 32.7 / 50.9 ms (localhost; both include the receiver's frame pacing), a sender-side 0.3 % drop recovered by key requests, 2101/2302 frames shown. Native match_net/match_flow pass. Next: pt.menu.

## 2026-10-13 — pt.menu PASS (branch online-ha)
- ONLINE is a real item of the game's main menu (second, items 22 apart to fit the panel): the main menu's binary .rfe (the user's data, decoded by runtime/core/fe_xml.c: a DOM of 9-u16 element records, 1-based table/pool indices) is edited as it loads and handed to the game's loader as XML text (override 0x5A06D0). The party's screens (ONLINE 7100, PARTY 7101 = host's party / friend's lobby, JOIN 7102 with a letter grid) are built from the main menu's own parts (panel, highlight bar, frame, button style, animations) and loaded for their ids (override 0x4EBAE0); buttons' X press = the unused developer callback 0x574380 (override → fe_party.c by the button's text); Triangle = fez:GoToScreen.
- Found by measurement: FeSetCurrentScreen's texture step (0x4EBD6A) hangs for an id the registry does not know (null texture list, a loop that never advances): our ids go through as the main menu's (override 0x4EBD60). The menus' cursor is [FE+0x31488] (+8/+0xC floats): in the browser it is placed at the pointer (mouse, touch), no pointer lock, no DirectInput mouse deltas.
- Host 'M' messages (its front-end screen; the relay keeps the latest) → the friend's lobby says what the host is doing. Server errors "No party with that code" / "Party full". F2 overlay and its checks removed (ui_badge.c keeps the party badge). Scripted --replay runs keep the original menu (RT_ONLINE=1 to test the item).
- Check tools/net/party_menu_check.py: host keyboard (code, CONTINUE, badge), friend by keyboard typing, friend by mouse clicks on the grid, wrong code error, lobby "THE HOST IS PICKING TEAMS (PLAY NOW)", LEAVE PARTY; native scripted pad picks a letter. Re-run: party_controllers, party_webrtc (now judged on transport latency: localhost p95 20 ms vs relay 29 ms; frame pacing dominates in-game latency). Next: pt.sides.

## 2026-10-13 — pt.sides PASS (branch online-ha)
- runtime/core/party_sides.c: the host's Team Select (screen 6) decides; its callbacks (game/overrides/menus/team_select.c: team up/down, kit left/right, category L1/R1, CONTINUE, BACK) ask party_ts_allow: the sender (FUN_0081A5E0: the controller with input) must be its side's captain (host if on it, else the lowest player number) for team/category/kit, the host for CONTINUE/BACK.
- The host sends 'T' (each player's side; both sides' category, team, kit as the toggles show them; relay keeps the latest); friends' games follow the host into Team Select and back to their lobby, their controllers mirror the party (pad_attach.c: keyboard entry = host, pad slots 1..3 = friends, sides from 'T'), their toggles are stepped by the game's own callbacks until the option texts match (texts, not indices: lists can differ by profile); a friend's own keys there only go to the host.
- Learned: a toggle = option list (+0x154 head, +0x158 current; option {char *text, next}); FE+0x31478 = the side of the controller whose message is handled.
- Check tools/net/party_sides_check.py (3 pages): sides HAA-, the second away player refused, the away captain and the host change teams, both friends show the same Team Select and controllers, only the host continues, friends back to their lobby. Regressions: party_controllers, match_flow pass. Next: pt.camera.

## 2026-10-13 — pt.camera PASS (branch online-ha)
- A friend no longer copies the host's 16 camera floats: each game frames the puppeted match with its own CameraManager and own options (style +0x230 etc.). Its own camera was static: the CameraManager picks its named cameras on match events (listener FUN_00407810; Kickoff, Scrum, Lineout X, Zoom X, the style camera on 0x12/0x1B) and a friend's skipped logic posts none. The host forwards the camera events ('C', id+int) and a friend's listener (found in its CameraManager by vtable 0x8CCC88) hears them — not the personal ones (0x118/0x119) nor those that read match objects (0x3A, 0x41/0x4A lineout: a measured fault on the friend; 0x1A, 0x79).
- docs/PARTY_RESEARCH.md "Personal settings": every personal option lives in the options object 0xA33320 (outside the setup block): camera style/sliders/presets, HUD, names, volumes, video, vibration; difficulty is copied into the setup (the host's).
- Check tools/net/party_camera_check.py: host Side/Zoom Side/Kickoff, friend (RT_TEST_CAMERA=2) Broadcast/Zoom Broadcast/Kickoff; the friend's target follows the ball (corr ~0.8, host ~0.75). match_puppet_check now judges positions only (pictures reported: own cameras). Test tools: RT_MS_CAM (camera + ball trace with the camera's name), RT_TEST_CAMERA.

## 2026-10-13 — pt.pause_leave PASS (branch online-ha), and a Team Select fix the user reported
- Pause: Esc (and pad Start, mapped to Esc on that machine) pauses everyone; each player's pause menu is its own (the host ignores friends' pads while paused, so personal settings stay personal); a friend's RESUME sends 'Y' and everyone resumes; the host keeps a friend's pause request until its match pauses (Esc is ignored inside set-piece menus); REPLAY does nothing in a friend's game (pause_replay.c); a friend quitting the match leaves the party.
- Leaving: party_leave.c: the host unplugs the leaver's controller and, in a match, rebuilds the teams as the game does at half time (0x6AC5E0, 0x62CFC0 home/away, 0x6AC780): the AI takes the player; everyone gets a toast ("PLAYER 3 LEFT"). The host leaving closes the room ("ended"): friends leave their match for their main menu. A mid-match joiner follows into the match and watches (no side). 'S'/'M'/'T'/'E' always go through the relay (it keeps them for newcomers; over WebRTC it never saw them).
- User-reported bug (party joiner could not take a side: the icon came back to the centre): a tap's press and release could both arrive between two host frames (only the release seen), and a move on the joiner's own screen never reached the host. Fix: the host keeps a friend's presses until its game reads the pad; a joiner's local icon move becomes stick pulses to the host until its Team Select has it there. Measured: 30 ms taps now move the joiner's icon on the host every time.
- Check tools/net/party_pause_leave_check.py (4 pages); match_flow_check now waits for the host's room before the friend joins (a race: the friend sometimes created it) and its friend resumes from its own menu. Next: pt.modes.

## 2026-10-13 — user playtest of the host-authoritative party; switch to whole-session lockstep (ls.*)
- Fixed from the playtest: an unfocused browser window no longer deactivates the game (the host froze and went black, which also bounced a joiner's Team Select moves); a joiner's HUD bars follow the host's controls; a leaver's pad leaves its side (no reassign screen on friends); the host leaving mid-match sends joiners to their main menu. Commit d5910c1.
- The user then asked for each player to play "as if alone", the same match, like rugby games do online. Joiners lacked everything the host's match logic drives (penalty options, kick aim, banners, cutscenes, referee). Measured: two copies of the same match differ from frame 1 after different menu histories (match-only lockstep is not free). The user chose whole-session lockstep (docs/plans/2026-10-13-lockstep-party.md, ls.* in features.json).
- ls.boot measured: lockstep from boot runs 60 fps in menus and match in the test browser; native drop-in and web drop-in still pass. Lockstep fixes: pads come only from the input stream (a host's gamepad no longer written to pad 0 on every machine; plugged-in = roster + the input's own flag), the host's keyboard is no longer also its pad, the party screens' logic runs while catching up, the browser pointer is recorded in the game's window units.
- Next: ls.boot catch-up time, then ls.party (ONLINE screens drive the session: code, JOIN reloads into the room, LEAVE).

## 2026-10-13 — ls.party and ls.controls PASS (lockstep party)
- ONLINE screens drive the session (CREATE shows the code, JOIN asks the server then reloads into the room, LEAVE); the page only wraps the game + fps; all messages and the joining screen are drawn in the game (user's rule).
- Nobody waits: joiners get their start frame and the others' front from the server and send idle inputs while catching up (beyond the 1024-frame input ring: kept as a range); stalled players (3 s) are dropped; render-target readbacks shared ('X'). Play logs of every page go to logs/play/ (server/playlog.go).
- Checks: ls_party_check (3 pages, wrong code, 204 equal hashes, leave), ls_controls_check (solo has no phantom pads, friend's pad plugged, sides K:H P1:A in both games, leaver unplugged).
- Next: the user's play test (match, mid-match join, leave, lag); then ls.match/leave/lag/modes checks from their feedback; ls.ha_out.

## 2026-10-13 — ls.match, ls.camera PASS; party roles and pause as online sports games (user play test feedback)
- ls.match: a party match is the same on both screens (hashes, RT_PIX pictures); Start on any controller pauses (pad_pause.c: the PC game pauses on Esc only). A joined game no longer fast-forwards/idles ahead (the catch-up runaway that dropped a friend's presses); a slot's previous player's 'D' no longer removes its newcomer; one build per party (stamped index.html, server refuses another).
- User feedback (play test): each player must play as if alone. Done: the host drives the shared menus (others: "THE HOST IS CHOOSING", Esc to leave), the pauser drives the pause menu (3 pauses a match, 60 s; others: "PAUSED BY PLAYER N"), another team's set-piece options covered, "YOU: PLAYER N" tag, hidden tabs keep pace, a dropped player returns to its own game, mid-match joiners come in after the match.
- ls.camera: own camera per player. Learned: HumanMove reads sticks against the camera (BackendState+0x64 -> +0x190 and a virtual on the camera), so the match must see one camera: player 1's look at the ball, set every frame after drawing (the camera step does not always run); each game's own camera is restored for its step and drawing; friends' sticks are turned into player 1's direction; the party hash is the match's state (ls_simhash.c), drawing state differs per player. Native: Side vs Broadcast/Truck positions equal; browser: Side vs Truck 162 hashes equal.
- Next: the user's play test of this build; ls.leave/ls.lag checks (written), ls.modes (GAME MODES needs PLAY NOW's tutorial prompt skipped once), line-out calls and kick aim shown only to the deciding player.
- OPEN (user play test 2026-10-13 23:32): the host's game crashed right after QUIT MATCH (~25 min match, pauses, a mid-match joiner waiting): the EA audio 10 ms timer thread (0x7D62D0 -> 0x7D0470) called a callback node whose function is a heap address (ICALL unresolved 0x026B8046): a node of a freed object still in the callback list. Not reproduced by a 60 s solo match + quit. Next: when the list is changed and freed vs when the audio thread walks it under det scheduling.

## 2026-10-14 — browser mouse: the game's cursor under the pointer (4521d3a)
- Cause: the canvas was resized to the 640x480 back buffer behind SDL's back, and the game's 1920x1080 desktop window (y CW_USEDEFAULT) kept the SDL window that size: the pointer came in 1920x1080 coordinates, the game's mouse clamps to 640x480 (cursor stuck in the top-left third); SetCursorPos got y = INT_MIN.
- Fix (web only, native unchanged): the device sizes the window (plat_gl_set_size), plat_window_move leaves the canvas alone, plat_window_rect is (0,0, canvas) also in lockstep.
- Proof: headless probe (pointer at 5 game points, 470x595 portrait page): cursor tip within 4 px of the pointer at all 5; clicks at 320,239 for the canvas centre (was 959,540).

## 2026-10-14 — ls.joinview PASS: joining shows the party screen, "(YOU)" on each player's row (branch online-ha)
- A joiner never sees a black screen: on JOIN PARTY accepted (server peek now gives the slot and players) its own game shows that party's screen with its row and a 0% bar; the page keeps a JPEG of it across the reload (platform/web/join_view.c, shell.html); while catching up the picture is drawn with the live bar, and once the replay is on the party screen its own frames are drawn (d3d8 ff_drawn: one frame in 16).
- "(YOU)" on this player's roster row in every game. Learned: the FE text setter 0x83BAB0 frees/allocates in a block heap (0xAA2620) by length, so per-player text is lockstep-safe only if every game's row is as long and set at the same frames: rows padded to 24, setter driven by the shared text.
- The front end is 640x480 drawn 4:3 centred in a 16:9 back buffer (overlay mapping in ui_badge.c catchup_row).
- Next: rugby08-72's friend lobby view (ui_party_view.c) reuses catchup_row/backdrop; ls_party_check rerun on this build.

## 2026-10-14 — party: each player's own cursor; a friend stays on the party screen while the host chooses
- Rule from the user (CLAUDE.md): share only what professional online sports games share; a friend never sees the host's menus or cursor.
- ls_cursor.c: a friend's screen draws the menus' cursor at its own pointer (BeginScene..Present only; the shared, host-driven position is back before any logic). ui_party_view.c: while the host is in its own menus, a friend's screen shows the party screen (read back when last drawn, d3d9w_frame_read) with the message over its buttons; rows redrawn if the roster changed. Lockstep untouched (drawing only).
- Proof: the user's two-window test (cursors independent, party view on the friend, Team Select shared); structure/lint ok. tools/net/ls_cursor_check.py written (equal pictures with both pointers in one place); its first full run is pending.

## 2026-10-14 — joining: the party picture through the page load; the others see a joiner's row fill up (user-tested)
- shell.html keeps the party's picture up (no "Downloading" text over it) until the game shows its first frame (Module.rtFrames), so the joiner's page never goes black between JOIN and catching up. User play test: no black screen.
- The host (and anyone already in) sees a joiner's progress: on the party screen its row's bar and percent, elsewhere in the menus the badge "PARTY C n/4 - PLAYER k JOINING p%" (server members joining/pct -> Module.rugbyMembers -> plat_join_progress).
- ls_joinview_check now screenshots the joiner's page every 0.4 s from JOIN to caught up (fails on >= 0.8 s black), requires the host's row trace and the overlay at one menu x throughout.
- Fixed from the headless run: the picture came too late at low frame rates (fallback reload after 4 s, picture at 60 frames) and, taken earlier, mid screen transition; after the reload the replay had no main menu yet, so the bar sat at menu x 0. Now: picture at 60 frames, fallback 20 s, the menu's x kept with the picture (plat_join_menu_x). Check ok: never black, menu x 106 in all 218 overlay traces, 129 hashes equal.

## 2026-10-14 — the user's play test: friend removed at kick-off, rejoin reset, LEAVE PARTY restart, freeze after QUIT
- Research and measurements: docs/plans/2026-10-14-online-fixes.md. Causes: "(YOU)" written into the game's memory differently per player (the lockstep clock split in the match loading: mirrored kick-off, desync, kick); a rejoiner replaying its slot's earlier 'A'/'D' as its own (exit 4); LEAVE PARTY was a page reload; EA sound banks reloaded at a new address after a match never resolved (aems.c), the audio thread crashed.
- Fixed: rows the same in every game, "YOU" drawn over (ui_badge.c); "since" from the server (rejoin); leave in place (server/fork.go: a room of its own with the party's log + the others' 'D' at G; net_leave.c: host = longest in the party, the desk follows it in the menus, the leaver to its main menu through a desk event); lost connection plays on alone (no reload); aems.c moves banks; canvas keeps its drawing (a mid-frame wait for the host's readback blanked a friend's frame).
- Proof so far: match run with both games entering at the same frame and every match hash equal (2 runs); quit_sound_check ok (new item aems.rebase); server fork_test ok. Open: one run's clock split 25 frames into the match load (match still equal), traced with RT_DET_SCHED.

## 2026-10-15 — online-ha: the user's play test fixed at the root (desync, camera, kick arrow)
- Desync (friend dropped at frame 27719): own_end.c asked the game for its camera's name (a guest call) in the friend's game only; the extra guest entries moved the lockstep's thread switches (the loader's lines swapped at a scrum, heap free counts parted 8000 frames before the hash). ms_camera_name is now a det_local section.
- Camera: filtering possession events (0xAD, 0xAE, 0x1A) does not stop the end camera following the ball. Each game now draws the shared camera straight down the pitch along its own team's way (d3d8_pov.cpp rt_pov_way), the way read off the camera while that team holds the ball. ls.own_view PASS (scheduling equal over 23642 frames).
- Kick aim arrow: own_hud.c leaves the other team's aim out of the picture (meshes known by vertex data; acting team = ball holder, else the last event's player). ls.match PASS (pictures equal outside the per-player prompt, 225/225 hashes). Penalty options fall back to the acting team.
- Master got the general fixes without the online mode (b858e74, the user's request). Next: the 2D prompts (play calls, FEED/BIND, ruck wheel), lag on two machines.
