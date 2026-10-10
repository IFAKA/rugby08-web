# Plan: stop fighting the protector — Wine oracle, interpreter fallback, clean exe

## Context
After about 6 working days, the game boots and renders the menu natively (`native.menu` passes), but it crashes during play. Almost every open item is caused by the copy protector, not the game:
- protector stubs
- decrypt stubs in switch tables
- the runtime-filled call dispatcher at 0x1055000
- junk after jumps
- 305 fall-through targets that were never lifted

Projects that finish recompiling in one night have clean binaries, so finding all the code is easy for them. We have been reversing the protection one address class at a time. Each round removes some crashes, but every miss still kills the run, because `RECOMP_ICALL`/`RECOMP_ITAIL` → `recomp_miss` (runtime/core/cpu.c:215) logs the miss, then returns eax=0 or drops the jump. The bad values that follow (e.g. ITAIL to 0x00000000 and 0x002FFD60 from 0x6F6292 in logs/play-20261004-162922.log) come from that.

What changes:
1. Get an oracle: run the real exe under Wine.
2. Make a miss non-fatal: interpret code that wasn't lifted.
3. Produce a clean, deprotected exe and lift that.

Each phase is a features.json item with an exit-0 check, so any later session can resume it through the normal session protocol.

## Resume here (status)
- [x] P0 persist plan (docs + features.json + PROGRESS)
- [x] P1 `oracle.wine` (passes; see PROGRESS 2026-10-10)
- [x] P2 `interp.fallback` (passes) → `native.smoke` (passes; needed lift.reachability, see PROGRESS)
- [ ] P3 `deprotect.exe`
- [ ] P4a web runtime: JSPI yield + lazy WasmFS fetch of game data (web.smoke passes; page stalls at frame 1)
- [ ] P4 `web.interp`

## P0 — Persist (first thing on execution, ~5 min)
- Copy this file to `docs/plans/2026-10-08-oracle-fallback-deprotect.md`.
- Add the new items below to `features.json` `items`. Add them only; never delete or weaken existing checks (per `_rules`). Make `native.smoke` also depend on `interp.fallback`.
- Add 3 lines to PROGRESS.md under a new heading: the strategy change and a pointer to the plan doc.
- `krn state add fact "Protector is the bottleneck; strategy = Wine oracle + interpreter fallback + deprotected exe (docs/plans/2026-10-08-...)"`.
- Commit (code/docs only).

## P1 — `oracle.wine`: does the real exe run at all? (~1–2 h)
Why first: it's cheap, and it tells us whether `Rugby08_unpacked.exe` itself is a good unpack. If it also crashes under Wine, the problem is the unpack, not the lifter, and the plan pivots to P3 immediately.
- Host: arm64 Mac, 32-bit PE. Needs Wine with WoW64 (32-bit) support under Rosetta. Install route: `brew install --cask wine-stable` (or the user's CrossOver/Whisky). **Ask the user before installing.**
- New `tools/run_wine.sh [exe] [--seconds N]`:
  - runs the exe in a private prefix under `build/wineprefix` (gitignored) with the `Game Files/` data next to it
  - sets `WINEDEBUG=+seh,+loaddll` to `logs/wine.log`
  - prints one summary line (alive after N s / exit code / first exception)
- Check: `tools/run_wine.sh Rugby08_unpacked.exe --seconds 60` exits 0 when the process is still alive at 60 s and `logs/wine.log` shows d3d8 loaded with no unhandled exception.
- The user then plays once under Wine to confirm a match is playable. This is the reference behaviour for later comparisons (allowed: the user playtests).
- If it fails: write BLOCKED in PROGRESS with the Wine exception and go straight to P3. P3 must then also repair the unpack.

## P2 — `interp.fallback`: a miss runs slowly instead of crashing (core change, ~1–2 days)
Design: on a dispatch miss, run guest x86 in Unicorn on the **same guest memory**, until control reaches lifted code again.
- **Library:** Unicorn (C API). It's already the reference CPU in `tools/difftest.py` and `tools/stubemu.py`, has full x87, and runs on arm64. Native: `brew install unicorn` (ask) or vendor it under `runtime/third_party/`. Link it in CMakeLists (native only for now, behind `RT_INTERP`).
- **New file:** `runtime/core/interp.c`, declared in `runtime/include/rt.h`: `void rt_interp(uint32_t va, int tail)`.
  - **Memory:** `uc_mem_map_ptr` the guest arena (`G2H(0)`, `rt_guest_size`) so there's no copying and both sides see the same writes. Map in large chunks; skip unmapped holes.
  - **Registers:** copy `g_eax..g_edi, g_esp, g_ebp`, flags (`g_flag_*`, `g_flag_cf`) and x87 `g_st[]`/top into Unicorn, then copy them back when it exits. Reuse the register/flag mapping from `tools/difftest.py` and `difftest_harness.c`. Don't invent a new one.
  - **fs / TEB:** set up a GDT entry in Unicorn so `fs:[0]` hits the same TEB the runtime uses (`k32_eh.c` walks fs:[0]). Copy how stubemu/difftest set up segments, if they do.
  - **Leaving the interpreter:** `UC_HOOK_BLOCK`. When a block's start VA has a lifted entry (`recomp_lookup`/`recomp_lookup_manual`) or is an import thunk (`recomp_lookup_import`):
    1. stop Unicorn and copy registers back;
    2. read the return address at `[esp]` if this was a call;
    3. call the native function (it pops the return address as usual);
    4. resume interpreting at the return address, unless the return address is `RECOMP_RETADDR`. If it is, return to the lifted caller.
  - **Returning to native:** a `ret` whose popped value is `RECOMP_RETADDR` → stop and return to the lifted caller. That's the `RECOMP_ICALL` contract: it pushed RETADDR.
  - **Faults:** an unmapped fetch (VA 0) or a #UD stops the run and writes a full report to `logs/interp.log`: the VA, the last N blocks, and the `g_icall_trace` ring. These are real bugs to fix by class, not to ignore.
- **Hook point:** in `RECOMP_ICALL`/`RECOMP_ITAIL` (runtime/include/recomp/recomp_types.h:941,970), replace the miss branch: `recomp_miss(...)` keeps logging, then calls `rt_interp(_va, tail)` in place of `eax = 0`. Also route the `#UD empty entry` fault stubs (108) and fall-through targets that were never lifted through `rt_interp`. Find where lift.py emits them (`fault_stubs`, fall-through ITAIL).
- **Interpreted code calling back into lifted code** nests naturally. `rt_interp` must be reentrant (one `uc_engine` per thread, TLS).
- **Feedback for speed, not correctness:** append every VA where interpretation started to `logs/interp_entries.txt`. `tools/lift_fixpoint.sh` may ingest them as discovered entries, the same way `tools/icall_seed.py` already does for ICALLs. This is execution-discovered entries, not hand seeds.
- **Check `interp.fallback`:** new mode `tools/difftest.py --interp --sample 500`, built on the same harness:
  - runs sampled leaf **and non-leaf** lifted functions through `rt_interp` instead of the lifted body;
  - compares against the lifted result;
  - exits 0 iff ≥500 compared, 0 mismatches, and calls/returns into lifted code round-trip.
- **Then `native.smoke`:** `tools/run_native.sh --frames 600` exits 0. The user then playtests a match through `scripts/play.sh`. New `logs/interp.log` faults are fixed as classes.

## P3 — `deprotect.exe`: a clean PE as lifter input (~2–3 days, can run in parallel with P2)
Turn `tools/patch_exe.py` from "Ghidra only" into the real deprotector. Each rewrite reuses a resolver we already have:
1. 933 `push/push/ret` → `call`/`jmp`: `tools/code_patch.py` (done).
2. Protector pointer stubs `jmp [slot]` (1272): write the resolved target as a direct `jmp rel32` at the stub site. Resolution is already in `tools/build_catalog.py`.
3. Switch slots that point at decrypt stubs (51 sites / 90 slots): write the resolved case address into the table slot (`stubemu.target`).
4. The runtime-filled dispatcher at 0x1055000: emulate its init statically with `tools/stubemu.py`, dump the filled table, and rewrite its call sites as direct calls (open item 1 in PROGRESS NEXT).
5. Leave protector junk in place. Lift reachability already skips it.

Steps:
- Output `build/deobf/Rugby08_deobf.exe` and print one line with counts per class.
- **Oracle check:** `tools/run_wine.sh build/deobf/Rugby08_deobf.exe --seconds 60` behaves like the P1 run (alive, d3d8 loaded, no exception). Then the user plays one match on it under Wine.
- Point `tools/lift_fixpoint.sh` / `tools/build_catalog.py` at the deobf exe. Rerun the fidelity report: `lift.fidelity` and `lift.reachability` should improve sharply, and stubemu special cases become dead code. Remove them only after the checks pass.
- Check `deprotect.exe`: patch_exe exits 0, the Wine run on the deobf exe passes, and `fidelity_report.py` shows `fallthrough_targets_not_lifted` lower than on the protected exe.

## P4a — web runtime architecture (found 2026-10-10, before web.interp)
web.smoke passes (WinMain in headless Chrome), but the page stops after frame 1:
- The game's Windows loop runs on the browser main thread and never yields (no PROXY_TO_PTHREAD, no
  ASYNCIFY/JSPI), so the browser cannot present or deliver input. Fix: link with `-sJSPI` and yield in
  the d3d8 Present path (`emscripten_sleep(0)`); JSPI suspends wasm without Asyncify's size cost
  (143 MB module). Fallback if JSPI misbehaves with pthreads/tail calls: `-sPROXY_TO_PTHREAD` +
  OffscreenCanvas.
- 1.7 GB of game data and no loader in platform/web (build/web/data + manifest.json are never read;
  the runtime reports FILE_NOT_FOUND data/data.gob). Fix: WasmFS fetch backend
  (`-sWASMFS`, `wasmfs_create_fetch_backend`) mounted at the game-files root, fetching on first read
  from the local server (tools/serve.py needs Range support, or read the 8 MiB chunk files).
- Check: web.menu — headless Chrome screenshot of the canvas shows the title/menu (as native.menu).

## P4 — `web.interp`: the web build
- Try building Unicorn with emscripten (there are prior builds, e.g. unicorn.js). If it builds and the size is acceptable, enable `RT_INTERP` in the web build.
- If it doesn't, ship the web build without the interpreter. Its lift then includes every entry from `logs/interp_entries.txt` gathered in native playtests (P2 feedback). On web, a miss logs and stops cleanly.
- Then `web.smoke` per existing features.json.

## Critical files
- `runtime/include/recomp/recomp_types.h` (ICALL/ITAIL miss branch)
- `runtime/core/cpu.c` (`recomp_miss`)
- new `runtime/core/interp.c`
- `runtime/include/rt.h`, `CMakeLists.txt`
- `tools/patch_exe.py`, `tools/code_patch.py`, `tools/build_catalog.py`, `tools/stubemu.py`, `tools/lift_fixpoint.sh`, `tools/icall_seed.py`
- `tools/difftest.py` + `tools/difftest_harness.c` (`--interp` mode)
- new `tools/run_wine.sh`
- `features.json`, `PROGRESS.md`

## Verification
- P1: `./init.sh check oracle.wine`, plus the user plays under Wine.
- P2: `./init.sh check interp.fallback` (difftest `--interp`, 0 mismatches), `./init.sh check native.smoke`, plus the user playtests through `scripts/play.sh`. `logs/interp.log` is the crash referee.
- P3: `./init.sh check deprotect.exe`, then `lift.fidelity` and `lift.reachability` rechecked on the deobf input.
- P4: `./init.sh check web.smoke`.

## Rules carried over
- All new tools print one summary line, with details in `logs/`.
- Heavy steps go through `krn exec --cache`.
- No per-VA hand seeds or null guards.
- Nothing EA-derived committed; `build/wineprefix` is gitignored.
- Installs approved by the user (2026-10-08): `brew install --cask wine-stable` (Rosetta needed) and `brew install unicorn`. Any other install still needs asking.
