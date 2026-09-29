# Rugby 08 → browser (static recompilation)

Objective: run the full Rugby 08 (PE32 x86, MSVC 7.1, D3D8/DInput/DSound) in a browser on localhost, with all original content, by statically recompiling `Rugby08_unpacked.exe` to C and compiling it with emscripten. Local only: nothing is deployed or pushed.

## Session protocol (every session)
1. `krn context --json`, then `tail -40 PROGRESS.md`, then `krn state show` if present.
2. Pick the first item in `features.json` with `"passes": false` whose `deps` all pass. Work on only that item until its `check` passes.
3. Run the item's `check` command. Set `"passes": true` only if it exits 0. Never mark an item passed on judgment alone.
4. Commit (code and docs only), append 3–5 lines to PROGRESS.md (what changed, what you learned, next item), then continue with the next item in the same session.
5. If blocked (the same item failed 3 approaches, or it needs the user), write `BLOCKED: <item id> — <exact reason>` at the top of PROGRESS.md, record it with `krn state add negative "..."`, and move on to an independent item if there is one.

## Context rules (the filesystem remembers; the context window only reasons)
- NEVER Read whole: `gen/**`, `analysis/**`, `ghidra/**`, `logs/**`, `third_party/**` sources larger than ~300 lines. Query them: `jq` for one record, `rg -m 20`, `sed -n A,Bp`, `tail`, `krn find QUERY --json --max-files N`.
- Every script in `tools/` and `init.sh` prints ONE summary line to stdout and writes details to `logs/<name>.log`. Keep it that way for new scripts.
- Heavy deterministic steps (analyze, lift, big builds) run via `krn exec --cache --input <each input> -- <cmd>` so repeats are free.
- Mechanical edits: `krn code replace|insert-before|insert-after|remove`. Checks: `./init.sh check <id>` or `krn verify --level fast --json`.
- Subagents: Explore only for wide sweeps where you need a conclusion, not file dumps. Once the runtime interfaces in `runtime/include/` are stable, independent shims (d3d8, dinput, dsound, win32 kernel/user) may run as parallel worktree subagents, each owning its own feature ids.
- Use `krn state` only for irreducible facts: objective, constraints, proven facts, dead ends.

## Hard constraints
- Never commit EA content or derived binaries: `Game Files/`, `*.exe`, `gen/`, `build/`, `analysis/`, `ghidra/` are gitignored. Keep it that way.
- Never publish or upload anything (no deploys, no pushes to remotes, no pastebins). Local only.
- Reuse before writing: pcrecomp (lifter/runtime), xboxrecomp and burnout3 (ICALL dispatch, D3D8 semantics), d3d8to9, d3d9-webgl, SDL2 and ffmpeg (VP6). All are in `third_party/`. Read their docs before reimplementing anything.
- Verification referee: behavior checks (trace logs, frame hashes, screenshots), never "it looks right".

## Layout
- `third_party/` submodules (pcrecomp, xboxrecomp, d3d8to9, d3d9-webgl)
- `tools/` our scripts · `runtime/` our Win32/DirectX HLE + glue · `platform/native`, `platform/web`
- `analysis/` pcrecomp/Ghidra outputs (ignored) · `gen/` lifted C (ignored) · `build/` (ignored) · `logs/` (ignored)

## Standing instruction (from the Opus 5.5 prompting guide, for unattended runs)
A standing instruction from the user, the person you are working for. It is about how your turns end. A message with no tool call in it ends your turn, and the work stops there until you are asked to continue. The user has seen you end turns in four ways while work they asked for was still owed, and does not want any of them. One: a long summary of what was done that closes by announcing the next step and has no tool call, so the next thing never starts. Two: an offer to carry on with something unless the user would prefer otherwise, which stops to wait for an answer the user was not going to give. Three: a list of decisions for the user when, by your own account, none of them blocks the rest of the work. Four: deciding that this is a good place to report, because the turn has been long or a milestone is done. Status notes are welcome, and so are your recommendations on open decisions, but put them in the same message as your next tool call and carry on with whatever does not depend on the user's answer. If you notice yourself inviting the user to redirect you or offering to wait, delete it and do the next thing. The stops the user does want are the ones where nothing can move without them, or where the thing blocking you is deliberately protected from you. This does not override the need for confirmation on risky or destructive actions.
