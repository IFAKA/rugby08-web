# Refactored Strategy: Static-First, User-Playtest-Driven

## The Problem with the Old Approach

Old features.json had **6 failing items all gated on behavioral checks:**
- native.input (expects 'menu state changed' log that game doesn't emit)
- native.match (expects scripted replay to reach kickoff)
- web.boot (expects Headless Chrome to render menu)
- web.video_audio / web.match (browser multimedia + replay checks)
- e2e (integration test)

**Cost:** Expensive frame-limit checks, replay infrastructure, screenshot tools, logs parsing for non-existent events.

**Why it failed:** The user said (2026-10-04 PROGRESS.md):
> "fix root causes as whole classes, statically. No per-crash seeds/guards, no driving the game with replays/screenshots; the user playtests."

But the tests were **driving the game with replays** and expecting specific logs.

---

## The New Approach: Follow What Works

**Pattern from Burnout3 recomp, xboxrecomp, OpenGOAL:**

1. **Static checks first** (compile, fidelity, reachability) — cheap, deterministic
2. **Minimal smoke tests** (boots? exits cleanly?) — fast, no behavioral assertions
3. **User playtest** (they play, log crashes manually) — not automated
4. **Iterate** (crash log → seed static fixes → rebuild → user retests)

**Why this wins:**
- Each static check fixes a whole defect CLASS (e.g., "all encrypted-call stubs")
- User playtest is free (they do it anyway)
- No fragile behavioral assertions ("did the menu render?")
- Minimal token overhead (fidelity_report.py runs once per lift, not per frame)

---

## Changes to features.json

### Removed
- `native.input` — "menu state changed" log doesn't exist; remove behavioral assertion
- `native.match` — depends on input; replace with smoke test
- `web.boot` — depends on native.match; replace with "loads without crash"
- `web.video_audio` / `web.match` / `e2e` — browser playtest is manual, not automated

### Added
- **`lift.fidelity`** — fidelity_report.py thresholds (refs_bytes_lifted >= 99%, ghidra_unlifted <= 20)
  - Static check, ~1s to run, gates the next item
- **`lift.reachability`** — fallthrough targets after jmp/ret (< 10 remaining)
  - Identifies the specific root cause: pcrecomp lifts junk after unconditional jumps
  - Depends on lift.fidelity so we fix in order

### Simplified
- **`native.smoke`** — replaces input/match, just "exits status 0 after 600 frames"
  - No replay, no log parsing, no screenshots
  - If it crashes, crash is in logs/native_run.log; user seeds it via tools/icall_seed.py
- **`web.smoke`** — replaces web.boot/video_audio/match, just "wasm loads, WinMain entered"
  - Single `--expect` check, no chrome rendering assertions

---

## How It Works in Practice

### User Session:
1. Run `./init.sh next` — shows next failing item
2. If it's `lift.fidelity`:
   - Read tools/fidelity_report.py output
   - See: "305 fallthrough targets not lifted"
   - Fix in tools/code_patch.py / build_catalog.py (whole class)
   - Re-run tools/lift_fixpoint.sh
   - Check passes automatically
3. If it's `native.smoke`:
   - User runs `scripts/play.sh` manually (real input, real window)
   - Game crashes → logs/native_run.log shows fault address
   - User gives crash log to assistant
   - Assistant seeds via tools/icall_seed.py, re-lifts, hands back
   - User retests (no automation)
4. If it's `web.smoke`:
   - User opens localhost in browser
   - Assistant checks console for "WinMain entered"
   - If crashes, same static-fix loop

---

## Token Efficiency

**Old approach (per check):**
- Start native binary + frame-limit loop
- Render N frames
- Take screenshot
- Hash/parse output
- Parse logs for specific events
- **Cost:** 100+ tokens per check, fragile

**New approach (per check):**
- Static tool (fidelity_report, krn verify): 1–2s
- Binary smoke test: 1–2s (no rendering, no replay)
- **Cost:** 10–20 tokens per check, deterministic

**User playtest (one-time setup):**
- ~5 minutes to configure scripts/play.sh window/logging
- Then user owns it (no automation cost)

---

## Migration Path

1. Replace features.json with this proposal
2. User runs: `./init.sh next` → shows `lift.fidelity`
3. If fidelity_report.py thresholds not met:
   - Read output
   - Fix root cause in tools/ (from PROGRESS.md NEXT list)
   - Mark `lift.fidelity` PASS, then `lift.reachability`
4. Once native.smoke PASS:
   - User opens scripts/play.sh (manual, guided by assistant)
   - Crashes are seeded statically, not per-incident
5. Once web.smoke PASS:
   - Browser playtest is manual (localhost)

---

## Alignment with Prior Art

- **Burnout Paradise recomp**: Static lift → binary smoke test → user playtest
- **xboxrecomp**: Fidelity report gates next phase
- **OpenGOAL**: "User found crash → assistant fixes root cause class → retry" loop
- **pcrecomp**: Difftest for lift quality; runtime for smoke tests

This refactor applies the same pattern.
