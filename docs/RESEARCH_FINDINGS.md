# Web Research: Correct Recomp Approach (2026-10-04)

## Finding 1: SetCursorPos is Broken (Use SDL_SetRelativeMouseMode)

**Source:** [SDL2 Issue #5271](https://github.com/libsdl-org/SDL/issues/5271)

Windows 10 Fall Creators Update stopped sending mouse motion events for SetCursorPos(). This breaks relative mouse tracking in recompiled games.

**Solution:** Use `SDL_SetRelativeMouseMode()` instead.
- Hides cursor
- Reads relative motion without screen boundaries
- Works on all platforms (native, web via emscripten)
- No game-side code changes needed (guest SetCursorPos becomes a no-op)

**Action:** `platform/sdl/sdl_plat.c` - replace SetCursorPos wrapper with SDL_SetRelativeMouseMode.

---

## Finding 2: Runtime-Filled Dispatcher Tables (Emulate Statically)

**Source:** [pcrecomp emu_unpack.py](https://github.com/sp00nznet/pcrecomp/pull/5)

When protector stubs fill a table at runtime (e.g., 0x1055000 dispatcher), emulate the stub's initialization statically using Unicorn, then treat resolved targets as entries.

**Pattern:**
1. Run stub under Unicorn with fake kernel32 (LoadLibraryA, GetProcAddress, VirtualAlloc)
2. Capture final table state
3. Add targets to catalog as entries

**Your Case:** Six runtime-dispatcher sites (0xAC54E0, 0xAC66C0, 0xAC50B0) fill lookup tables. Extend `tools/stubemu.py` to resolve them.

**Why:** The 0x13A2D97 crash (NEXT item 1) is unreachable because the dispatcher table isn't known statically.

---

## Finding 3: Switch Table Arms Must Be Entries

**Source:** [xboxrecomp PR #6](https://github.com/sp00nznet/xboxrecomp/pull/6)

Switch arms outside the Ghidra-detected function bounds must be registered as separate entry points.

**Pattern:**
- Every `jmp [reg*4+T]` slot must resolve to a lifted entry
- Arms past function extent need their own labels
- Example: `jmp [ecx*4+0x402AAC] @ 0x4029A2 → stub 0x45E637 → case 0x4029A9`

**Your Case:** 51 switch sites / 90 decrypt-stub slots (NEXT item 2). Audit each `jmp [reg*scale+offset]` and ensure target is an entry.

---

## Finding 4: Unresolved ICALLs Use Runtime Dispatch

**Source:** [xboxrecomp README](https://github.com/sp00nznet/xboxrecomp)

When static resolution fails, all successful projects use:
1. Auto-generated dispatch table (binary search on function address)
2. Runtime `recomp_lookup(guest_va)` fallback
3. Log misses to `logs/icall_miss.txt`
4. Seed missed targets back into catalog

**You Already Do This.** Your 5220 seeds from the last playtest proved it works. No changes needed here.

---

## Finding 5: Timeline for Playable Match

**Source:** [xboxrecomp README](https://github.com/sp00nznet/xboxrecomp), [Burnout 3 Project](https://github.com/sp00nznet/burnout3)

**Burnout 3 reality:**
- Empty repo → boots & renders 3D: **~2 weeks**
- "Playable to main menu": hand-written scaffolding (not genuine lift)
- "Play a real match": still in progress (not yet documented as done)

**Rugby 08 current state:**
- Menu renders ✓ (frame 1800)
- Reached kickoff once ✓
- Crashed at Stadium Select (unresolved ITAILs)

**Estimate:** You're 60–70% toward "one playable match." Mouse fix + dispatcher fix = next iteration. Then iterate on remaining crashes.

---

## Finding 6: Rugby 08 Reversals Already Exist

**Source:** [PCGamingWiki Rugby 08 Widescreen Fix](https://community.pcgamingwiki.com/files/file/3777-rugby-08-widescreen-fix/)

The community has already reversed:
- Widescreen fix (resolution patching via hex edit or ASI loader)
- Resolution hex offsets (profile file @ offset 638)
- Mod installation methods

Not critical for "playable," but available if needed for display issues.

---

## Actionable Roadmap

### Immediate (This Session or Next)

1. **Fix mouse input** (1–2 hours)
   - Replace `SetCursorPos()` with `SDL_SetRelativeMouseMode()` in `platform/sdl/sdl_plat.c`
   - Rebuild, playtest Stadium Select

2. **Resolve dispatcher table** (2–4 hours)
   - Extend `tools/stubemu.py` to emulate 0x1055000 dispatcher init
   - Add resolved targets to `build_catalog.py` entries
   - Run `tools/lift_fixpoint.sh`
   - Rebuild, playtest

3. **Playtest:** Run `scripts/play.sh`, reach kickoff, note any crashes

### If Still Crashing

1. Seed new ITAILs from `logs/icall_miss.txt`
2. Audit switch arms (51 sites, 90 slots)
3. Run `lift_fixpoint.sh`, rebuild, test

---

## Session Handoff Checklist

- [x] features.json refactored (static-first, playtest-driven)
- [x] PROGRESS.md updated with strategy note
- [x] docs/REFACTOR_STRATEGY.md explains migration
- [x] Web research completed (this file)
- [ ] Commit everything to git
- [ ] Next session: `./init.sh next` → shows `lift.fidelity`

**Goal:** Play one full match (menu → team select → kickoff → end).
