# Online party: play Rugby 08 with friends in the browser (plan, 2026-10-13)

Worktree `/Users/faka/code/projects/web/rugby08-ha`, branch `online-ha`. Supersedes the UX parts of
docs/plans/2026-10-12-online-host-authoritative.md (its steps 1-5 are built and kept). Track progress in
`features.json` (ids `pt.*`, in order) and 3-5 lines per step in PROGRESS.md. Never push or deploy without
the user's explicit go. Work non-stop through the steps; stop only for a decision only the user can make.
**Start at `pt.freeze`.** The other failing items in features.json (deprotect.exe, web.interp,
web.split_check) are an older, unrelated backlog: not part of this plan.

## What the user wants (decided with the user, do not re-discuss)
- **Browser only.** One web build on the user's own server; friends on other networks open it in a
  browser. The Mac native build is a test tool, not part of the product.
- **Play normally or in a party.** Solo is exactly the original game. A party is created with a
  generated code; friends join by typing the code. Up to 4 players.
- **Like any sports game's online mode** (FIFA online friendlies), each player at home on their own
  screen, own controller, own camera, own personal settings. The host's game config defines the match.
- **Integrated in the game**, in its own menus and style, usable with gamepad, mouse, keyboard and touch
  (phones). No hotkeys (the F2 overlay is rejected), no launch flags, no HTML panels.
- **Every game mode that allows several players**, not only PLAY NOW.
- **Lowest lag possible**: WebRTC peer-to-peer, host input delay for fairness, friend-side prediction.
- Solid root-cause design, no workarounds. Own server only (no third-party services).

## UX (agreed)
Input everywhere: pad (d-pad, Cross, Circle), mouse/touch (click or tap the item), keyboard (arrows,
Enter, Esc). On phones: tap menus; on-screen stick and buttons in matches.
1. **Main menu**: a new **ONLINE** item in the game's own style (next to PLAY NOW). Solo is untouched.
2. **ONLINE**: CREATE PARTY · JOIN PARTY · BACK.
3. **CREATE PARTY → party screen (host)**: the code in big letters, "Give this code to your friends",
   four slots ("1 · YOU (host)", "2 · waiting…"). CONTINUE → the normal menus to pick a mode; a small
   badge ("PARTY K7P2QX · 2/4") stays in a corner of every menu. LEAVE PARTY ends it.
4. **JOIN PARTY (friend)**: six boxes and an on-screen letter grid (pad: move and pick; mouse/touch: tap;
   keyboard: type). Clear errors: "No party with that code", "Party full".
5. **Party lobby (friend)**: code, who is in, "Waiting for the host…" and what the host is doing
   ("Host is choosing PLAY NOW"). LEAVE PARTY any time.
6. **Side selection (Team Select) on everyone's own screen**, synced: only party members' controllers
   appear, each with its player number; each player moves their own icon HOME/AWAY with their own
   controller and sees everyone's icons move live.
   - Each side's **captain** picks that side's team and kit: the host if on that side, else the lowest
     player number on it. Teammates see the choice live. A side without humans: the host picks its team.
   - The host presses CONTINUE; stadium, weather, length, difficulty: the host. The game's own rule
     stays ("at least one user controlled team"). Screens only the host uses show friends
     "Host is choosing…".
7. **The match**: the same match on everyone's own screen; each controls their own player (several
   humans on one side as on a couch); everyone sees the others' controlled players marked. Personal and
   per player: camera (style, angle, zoom), HUD options, sound/commentary volume, controller layout.
   Shared (host): teams, kits, stadium, weather, half length, difficulty, rules.
8. **Pause**: Start (Esc, a touch pause button) pauses the match for everyone; the pause menu shows on
   everyone's screen and the pauser drives it; personal settings (camera…) are changeable there.
9. **After the match**: wherever the mode goes next, for everyone. PLAY NOW: result screens, then host
   in its menus, friends in the party lobby. Tournaments (World Cup…): everyone follows the standings and
   fixture screens; the host drives, friends act where their side needs input.
10. **Leaving = closing the page** (or LEAVE PARTY): the player's controller disappears, the AI takes
   their player, everyone sees "Player 2 left". The host leaving ends the party: friends go back to their
   own main menu with a message. Someone joining during a match watches until the next Team Select.
11. **The page** only runs the game: it fills the window and resizes with it; the fps counter stays.

## What is shared in real time (and lag)
Host → friends every match frame (~55/s): players, animations, ball, score, clock, events (the game's
own replay record, ~380 B/frame, keys raw). Friends → host: their pad (12 B) every frame. Shared screens:
side selection, pause, tournament screens. Everything else (camera, HUD, sound) is each player's own.
The host has no lag; a friend sees their own action ~1 round trip later. Reduce it (in order): WebRTC
direct (one hop, no TCP head-of-line), then host input delay (equal feel), then friend-side prediction.

## Already built (reuse; details in docs/MATCH_STATE.md, docs/ONLINE.md)
- `runtime/core/net_ha.c` (host sends setup 'S' and frames 'F', friends send pads 'P', 'K' key request),
  `net_ha_flow.c` (friends follow the host into its match from any menu, out of their intro, back to
  their menus 'E'; pause 'Z'/'U'), `match_puppet.c` + `game/overrides/match/` (a friend's game shows the
  host's frames: positions, poses, ball, score), `match_setup.c` (match setup incl. kits), `match_camera.c`
  (currently re-applies the HOST's camera on friends: to be replaced by each player's own, step pt.camera).
- `runtime/core/ui_*.c` + `render/webgl/overlay.cpp`: an F2 overlay with HOST/JOIN screens and a 5x7
  font (to be replaced by the in-menu ONLINE screens; the overlay renderer and font may be reused for
  screens the game does not have, e.g. the letter grid, in the game's style).
- server/ relays S/F/K/P/E/Z/U unlogged and keeps setup + key for newcomers.
- Checks: tools/net/match_*_check.sh, ingame_ui_check.sh, cross_platform_check.sh (native, to be replaced
  by browser-only checks), tools/net/web_player.py (a Playwright browser player), match_compare.py.
- Tools: tools/query/callgraph.py (static call graph), tools/query/fn.sh, RT_COVERAGE diffs, `args:` hooks
  in tools/hooks.json (re-lift with `.venv/bin/python tools/lift/lift.py`, then build), forks for parallel
  read-only research (the user asked to parallelize what does not conflict).

## Known issues at the start
- **Test-browser freeze**: in headless Chrome (Playwright) the web game stops presenting frames during
  the intro movies / early menus (frame ~2400-3240); then DirectSound's cursor stalls and the game dies in
  its device-restore loop. The user's own Brave reaches the menus fine. It predates this work (bisect:
  fails at 15e8b14 too). net.web_dropin and the browser half of ingame_ui_check fail because of it.
  Unverified fix ideas from the investigation: `git stash list` → "freeze investigation (unverified)"
  (audio stall logging, short key taps remembered on slow pages). Find the root cause first.
- **Phantom controllers**: `rt_pad_count()` defaults to 4 (runtime/core/pads.c) because the game
  enumerates DirectInput once at boot (CPCControlMgr vf7 0x0081F1A0 → EnumDevices; calling vf7 again from
  Present crashed in FUN_00871EE0). Solo Team Select shows 3 idle pads; must go (pt.controllers). The
  reference checks pin RT_PADS=0.
- Team Select assigns only controller entries 1..4 (FUN_0081A5A0); with the keyboard first a 4th player
  cannot take a side (pt.controllers).
- Friends' games do not see the host's menus/Team Select yet; the host's cutscenes are not sent (friends
  hold the last frame); real 80-minute full time and half time untested (only quits).

## Steps (features.json `pt.*`, in order; each with an automated browser check)
1. **pt.freeze**: the web game runs in headless Chrome as in a real browser (two pages at once, through
   menus into a match, 10 minutes, no freeze). Unblocks every browser check.
2. **pt.research** → docs/PARTY_RESEARCH.md: (a) every game mode: several controllers or not, the screens
   players act on, how its matches start/end, between-match screens; (b) adding a real item to the main
   menu and building screens in the game's front-end system/style (FE screens vtable 0x0090F838, action
   table 0x991C80, fonts FFN in data.gob); (c) adding/removing a controller while the game runs (the
   game's own hot-plug path); (d) which code moves a human-controlled player (for prediction).
3. **pt.controllers**: solo = original (no virtual pads); a friend joining adds a controller that Team
   Select shows, leaving removes it; all 4 players can take a side.
4. **pt.webrtc**: party traffic over WebRTC data channels between browsers (unordered/unreliable for
   frames, reliable for setup/flow), the server only for signaling and as relay fallback; latency measured.
5. **pt.menu**: ONLINE in the main menu, CREATE/JOIN/lobby/LEAVE screens, all inputs; the F2 overlay goes.
6. **pt.sides**: Team Select on everyone's screen, synced, captains as above.
7. **pt.camera**: each player's own camera and personal settings (audit which settings are personal).
8. **pt.pause_leave**: pause for all from anyone (Start/Esc/touch), personal settings in the pause menu;
   leaving by closing the page (AI takes over), host leaving ends the party; mid-match joiners watch.
9. **pt.modes**: every multi-player mode in a party, including between-match screens (one check each).
10. **pt.page**: the game fills the window and resizes; fps counter kept; nothing else on the page.
11. **pt.touch**: phones: tappable menus, on-screen stick and buttons in matches.
12. **pt.fairness**: host input delay matching the friends' measured delay (host option, on by default).
13. **pt.predict**: the friend's own player moves at once, corrected smoothly from the host's frames;
   measured against the host's path.
14. **pt.lockstep_out**: remove the old lockstep mode (page panel, netplay lockstep paths, its checks
   and features items, docs).
15. **pt.deploy**: deploy-ready on the user's server (HTTPS: a domain + certificate or an HTTPS tunnel,
   decided with the user), docs/ONLINE.md as the players' guide. Deploying itself needs the user's go.

## Rules for the sessions
- Session protocol of CLAUDE.md (krn context, PROGRESS.md, first failing feature, check, commit).
- Browser-only verification for the product: host and friends in headless Chrome (Playwright, system
  Python /opt/homebrew/bin/python3, tools/net/web_player.py as a base). Native runs only as test tools.
- Test runs never block the user (hidden, muted). The user plays build/native in ../rugby08 (play.sh):
  never touch that worktree's build.
- Relays in checks pick a free port and fail if they cannot start (stray relays from other worktrees
  hold 8098 and 18097: never kill processes this session did not start).
- Measure everything (trace lines, compare tools, screenshots); root causes, no workarounds.
