# Online play: host-authoritative netcode (plan, 2026-10-12)

Branch `online`, worktree `/Users/faka/code/projects/web/rugby08-online`. Track progress in
`features.json` (ids `on.*`, in order) and 3–5 lines per step in PROGRESS.md. Never push or deploy
without the user's explicit go.

## What the user wants (decided, do not re-discuss)
- Online lives INSIDE the game (screens drawn in the game, driven by keyboard/mouse/pad), not in
  HTML panels. Same on the native app (Mac/PC) and the browser.
- Everyone stays in their own game with their own config the whole time. Host: Online → Host shows a
  code. Friends: Online → Join, type the code → a party (small in-game badge). The host picks the
  mode and match settings in their own menus. When the host starts, Team Select appears in every
  friend's own game; everyone picks a side with their own controller; the match is shared; whoever
  pauses drives the pause menu; after full time everyone is back in their own menus, party intact.
- Devices on different networks; up to 4 players; own server only (no third-party services).
- Solid, root-cause design. No workarounds (no drift "corrections", no hidden second game copy).

## Why host-authoritative (not lockstep)
Lockstep needs bit-identical simulation on every device; the native and browser builds already
differ from frame 1 (guest heap headers: 64- vs 32-bit host pointers), the slowest link stalls
everyone, and it cannot keep everyone in their own menus. Host-authoritative (Quake/Source,
SM64 Coop): the host's game runs the real match; friends send buttons; the host sends match state;
each friend's own game loads the same match and shows the host's state.

## Reuse
- Transport + rooms + codes: `server/main.go` (WebSocket relay, rooms, 6-char codes, TLS flags),
  `platform/native/net_native.c`, `platform/web/net_web.c` (`net_tx_*`).
- Input capture: `plat_local_pad` (pads), `plat_desk_*` (host keyboard/mouse) in `platform/sdl/sdl_plat.c`.
- Diagnosis: `RT_DET_PAGES`/`RT_DET_DUMP` (det.c, hex dumps in the log), `RT_LOG_INPUT`, `?env=` in the
  browser, replays (`tests/replays/play_now_match.txt` reaches a match natively), pyghidra MCP,
  `tools/analysis/rtti.sh`, wasm function names (`--profiling-funcs`).
- The from-boot lockstep (`runtime/core/netplay.c`, det.c, `net.*` checks) stays until on.match_flow
  passes, then its join/roster path is retired (det.c stays for replays/tests).
- det_hash() covers dlmalloc metadata, which holds HOST pointers: the hash depends on where the host
  mmaps the guest arena (measured on tidy: only --log/--user-dir path lengths differed -> different
  hash at frame 60, identical pixels). tidy pins the native arena (platform.c plat_guest_arena, hint
  0x10000000000). on.* checks therefore compare decoded match state (match_state.c), never heap
  hashes across processes or machines.

## Steps (each = one features.json item; finish its check before the next)
1. **on.match_map** — find the match in guest memory. Deliverable: `docs/MATCH_STATE.md` +
   `runtime/core/match_state.c` (`ms_active()`, `ms_phase()`, per player [30]: pos/rot/vel/anim id+time/
   team/role, ball pos/vel/holder, score, clock, controlled player per pad, match setup: teams,
   stadium, settings, RNG). Method: native det run of play_now_match.txt; page-hash diffs between
   frames to locate the hot structs; RTTI class names; Ghidra xrefs from the score/clock HUD draw.
   Check `tools/match_state_check.sh`: dumps every 60 frames; 30 players inside the pitch, clock
   advances 1 s / 60 frames in play, ball within 2 m of its holder, phases kickoff→play→(ruck|scrum)
   seen.
2. **on.match_setup** — serialize the setup on the host, apply it on a friend whose own game took a
   different menu route. Check `tools/match_setup_check.sh`: two natives (one visits MY RUGBY
   first), both reach kickoff with identical setup bytes and the same teams/stadium (screenshot diff).
3. **on.match_puppet** — friend mode: the local match simulation (AI, physics, rules) is suppressed
   and the received state is applied every frame; animation/rendering/camera/sound stay local.
   Check `tools/match_puppet_check.sh`: run A records host state to a file; run B (other menu route)
   plays it back: positions equal within 1 cm for 3000 frames, screenshots match.
   Design (found 2026-10-12, docs/MATCH_STATE.md "Replay system"): reuse the game's instant-replay
   path. The host's game records every match frame into its replay stream (PlayBack::CReplayManager,
   src/rep/replaymanager.cpp): a 0x118C-byte frame (30 players: 128-byte pose block, x/y/z/heading,
   u16 sequence; ball position and matrix; timestamp +0x116C; markers; flags) delta-coded against the
   previous frame (FUN_00458620 decodes, FUN_00457A40/457C40 pack/unpack, records carry their length).
   In replay mode the match logic is stopped and UpdatePlayers (FUN_0045A560) drives players, ball and
   poses (ReplayPoseSource) from frames A/B of the playback object. The friend stays in that mode and
   is fed the host's records: its simulation is suppressed by the game itself, the pictures come from
   the game's own playback code, and the wire format is the game's own compressed stream.
   As built (PASS 2026-10-12, tools/net/match_puppet_check.sh): playback mode itself is not used
   (replay camera, no HUD). The friend stays in its normal match and CRugby::Update's logic is skipped
   (game/overrides/match/rugby_update.c); per host frame it takes positions, headings and the ball the
   way UpdatePlayers does, the poses through the game's replay branch of FUN_004039B0 (host frame as
   frame A), the host's camera (16 floats per frame, FUN_006E8260) and score events (team stats +
   FUN_005CE320), and the kits come with the setup (+0x191C). Not replicated, by design or for later:
   the match phase (FUN_0062DA80 needs the set-piece objects the skipped logic creates: phase-driven HUD
   and the friend's own input UI belong to steps 4 and 6), the referee's pose, the friend's
   controlled-player marker. Wire per frame: 32-byte header, 64-byte view, 0x118C-byte frame (step 4
   compresses it with the game's delta coder).
4. **on.match_net** — host takes friends' pads from the network as real controllers; host sends
   state (quantized, delta vs last ack, ~60 Hz) through the server. Check: two natives over the
   server, friend's pad moves its player on the host, friend's view follows within 6 frames.
   As built (PASS 2026-10-12, tools/net/match_net_check.sh, runtime/core/net_ha.c, `--net-mode ha`):
   messages 'S' setup, 'F' frame (96-byte head + a raw key or the game's own replay record, ~380 bytes,
   ~165 kbit/s), 'K' key request, 'P' pad, relayed and not logged by server/ (it keeps the last setup and
   key for newcomers). Keys at start, on request and every 600 frames. Friends' pads are the host's
   controllers 2..4 (Team Select picks their side). Precision is the game's replay coder (1/52.5 m,
   whole degrees). Measured locally: latency median 0.5, p95 0.9 frames. To do in later steps: a friend
   who joins mid-match waits for a key (no catch-up needed); the match course depends on friends'
   input (a human side's scrum waits for it).
5. **on.match_flow** — party flow: host start → friends' games enter Team Select/match; pause by
   anyone pauses all and the pauser drives the menu; full time → everyone back in own menus.
   Check: two natives run the whole flow twice in one party.
6. **on.ingame_ui** — Online screens drawn by the runtime inside the game picture (overlay at
   Present, game-like style, keyboard/mouse/pad): Online → Host (code) / Join (type code) / party
   badge / leave. Remove the HTML panel. Check: native replay + Playwright: host code shown in the
   frame, friend types it with game keys, both in one party.
7. **on.cross_platform** — Mac native host + browser friend (and the reverse) in one match.
8. **on.deploy_ready** — server with TLS on the user's own server; docs/ONLINE.md for players.
   (Deploying itself needs the user's go.)

## Token rules for the session
Start: `tail -40 PROGRESS.md`, this file, `jq '.features[] | select(.id|startswith("on."))' features.json`.
Never read gen/, logs/, analysis/ whole: `rg -m 20`, `sed -n A,Bp`, `krn find`. Long runs in the
background, one summary line each. Answer user questions before acting; no hidden copies, no patches.
Another chat may edit this worktree (audio, perf: log.c, recomp_types.h, lift*.py): commit only your
own files.
