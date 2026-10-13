# Party play in lockstep (plan, 2026-10-13)

Worktree `/Users/faka/code/projects/web/rugby08-ha`, branch `online-ha`. Replaces the match part of
docs/plans/2026-10-13-online-party.md (host-authoritative puppet). Items `ls.*` in features.json, in order.
Never push or deploy without the user's go.

## Decision (the user, 2026-10-13)
After playtesting the host-authoritative party (the host's game plays the match, joiners show its
result), the user wants every player to play the match as if alone, the same match for everyone, "as
rugby online games do", without reinventing the wheel. Sports games online run **lockstep**: each
player's game simulates the whole game from everyone's inputs. Asked which kind, the user chose
**whole-session lockstep** (from boot, as the earlier netplay did) over match-only lockstep (measured:
two copies of the same match already differ from frame 1 after different menu histories) and over
keeping host-authoritative with piecemeal mirroring.

Consequences, accepted with that choice: everyone shares the host's menus (as on one couch, or FIFA's
shared team selection), each player drives their own controller everywhere; a joiner's page rebuilds
the game by fast-forwarding through the session so far (a progress screen), then plays along.

## What exists (reuse)
- runtime/core/det.c (deterministic mode: virtual clock, one guest thread at a time), det_hash.c,
  netplay.c + netplay_*.c (lockstep over the relay: inputs 'I' with delay, roster 'A'/'D', hashes 'H',
  drop-in by replaying the room log, authority hand-off), server/main.go (rooms, logs, members),
  platform/web/shell.html (connect, join, catch-up cover, offline fallback), d3d8_pov.cpp (a player's own
  view of the game camera, outside the game state). Checks: net.* (native and web), all passing again on
  this branch (net.web_lockstep, net.web_dropin measured 2026-10-13).
- The party's in-game screens (runtime/core/fe_*.c: ONLINE, CREATE, JOIN with the letter grid, lobby,
  badge, toasts), WebRTC (platform/web/js/rtc.js), the hidden-tab pacing and audio fixes.

## The rule that keeps it in sync
Anything that changes guest memory must come from the input stream (frame-stamped inputs and roster
events): never from local timing, local network state or page events. Local-only things (badge, toasts,
the catch-up cover, the camera POV) are drawn outside the game state (render/webgl/overlay.cpp, page).

## Steps
1. **ls.boot**: every web game boots in lockstep with its room open (the server logs it from frame 0;
   offline if the server is unreachable: solo exactly as before). Pass: menus and a match at >= 55 fps
   in the test browser with the session on; a hidden page keeps pace; equal hashes with a joiner.
2. **ls.party**: the ONLINE screens drive it. CREATE PARTY shows this session's code and the roster
   (from roster events: the same on every screen); JOIN PARTY (code) reloads the page into that session
   (catch-up cover with progress), then the friend is in the host's game wherever it is; LEAVE PARTY
   reloads into a fresh own game; errors (no such party, full) on the JOIN screen. Pass: 3 pages, joins
   after minutes of host play, catch-up time measured, equal hashes, identical frames (screenshots of
   all pages at the same game frame equal apart from local overlays).
3. **ls.controls**: each player's keyboard/gamepad drives their own controller (host: controller 1 with
   keyboard and mouse; friends: their slot's pad); solo shows only its own controllers; a joiner's pad is
   plugged and a leaver's unplugged at the same frame everywhere (roster events); Team Select is the
   game's own, 4 players can take sides.
4. **ls.match**: a full PLAY NOW match in a party: penalty options, kick aim, set-piece banners, cutscenes,
   replays, referee on every screen because every game plays them; each player's own view (POV presets
   in the game's options, local); pause from anyone. Pass: identical frames, equal hashes, the friend's
   presses act (a friend kicks a penalty).
5. **ls.leave**: closing a page or LEAVE PARTY: the AI takes that player (the game's own unplug path, no
   reassign screen); the host leaving hands the room on, the others play on.
6. **ls.lag**: inputs over WebRTC (relay fallback), input delay from measured round trips, smooth pace
   (no stalls on jitter); measured.
7. **ls.modes**: tournaments and the other multi-player modes (shared menus: they work as on a couch).
8. **ls.ha_out**: remove the host-authoritative puppet (net_ha*, match_puppet/camera/controls/live,
   party_sides mirror, game/overrides used only by it) and its checks; docs.
9. Then pt.page, pt.touch, pt.deploy (unchanged).

## Findings while doing ls.match / ls.leave / ls.lag (2026-10-13)
- A joined game fast-forwarded again whenever it trailed by 2·delay+60 frames, and while fast-forwarding it
  sent idle inputs ahead "so nobody waits": its presses were dropped and the others ran on, so it fell
  behind again (a friend flipped every 6 frames; with 3 players the session raced at ~130 fps). Now only
  joining fast-forwards and sends idle inputs ahead; in step, the others wait for real inputs (lockstep).
- The PC game pauses only on the keyboard's Esc: a friend's controller is a pad, so Start on any controller
  now presses Esc at the same frame in every game (runtime/core/pad_pause.c).
- The keyboard is player 1's: when player 1 leaves, it goes off its side (pad_attach.c) and the AI plays.
- A newcomer reusing a slot was removed by the 'D' of the slot's previous player (replayed from the log).
- One build per party: the page sends its build id (stamped at link time), the server refuses another.
- Pictures (RT_PIX) equal in menus and play; rare differences (1-4 frames a run) in loading screens and
  cutscenes are being traced (the game camera's hash logged next to the picture's).
