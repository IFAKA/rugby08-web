# Online party: every player plays as if alone (the user, 2026-10-14)

Goal: playable with friends online with the least lag, like EA FC Online Friendlies. Each player's game
looks and plays as solo; the only additions are the friends' markers and who is deciding when it is not
you. Any mix of up to 4 humans (home, away, or watching): 1v1, 2 vs AI, 2v1, 2v2, 3v1, 4 vs AI, ...

## Architecture (unchanged): lockstep
Every copy runs the same match from everyone's controller presses; only presses travel (plus a sync check
every 2 s, the shadow-map bytes once per match load, joins and leaves). The game's couch RULES stay (who
controls which player: they make it one match); the couch PRESENTATION (one camera for one TV, menus on
every screen) becomes each player's own. Per-player game code runs in det_local sections
(runtime/core/det_local.c): never preempted, clock reads not polls, entry count restored, so the shared
game's thread switches are the same in every copy (proof: RT_DET_SCHED traces equal).

## Measured facts (2026-10-14)
- Sync: full memory, clock and scheduling equal over a whole session (920 / 27630 frames) after the
  "(YOU)" text, cursor and padding fixes; ls_match_check passes.
- Camera, end-on (style 3 "Classic", the menu's ANGLE END ON; style 4 does not exist): solo it stays behind
  the human team whoever has the ball (Truck: 8 vs 9 samples; Classic: 7 vs 8, same direction); with a
  human on each team it follows possession (home ball: home end 14/15, away ball: away end 12/13), the
  same in both copies: the couch camera.
- The game marks each human's controlled player (coloured ring + name): the friends' markers exist.
- Catch-up (joining replays the session): natively 16000 frames in 55 s without drawing, 68 s with
  (drawing is 20%); profile: threads waiting their turn, game code, recomp_lookup (binary search per
  indirect call), TLS access (_tlv_get_addr, native). Browser: 82-200 s per join in the tests.

## User flows (EA FC Online Friendlies: invite, choose sides, set up, play)
A. Play: ONLINE > CREATE PARTY (code) - friend: ONLINE > JOIN PARTY, code (party screen, row filling) -
   host picks the mode (others: THE HOST IS CHOOSING) - Team Select: each moves their own controller to
   HOME / AWAY / middle (watch) - loading - match: camera from your own team's end with your settings,
   never flipping; your marker and your friends' markers; your decision: the menu; another's: the game
   carries on as when the AI decides, with "PLAYER N IS CHOOSING"; pause: anyone (3 a match, 60 s),
   others see "PAUSED BY PLAYER N", CHANGE CAMERA is yours only - full time - menus together.
B. Leave: LEAVE PARTY in the menus: your own main menu, your game goes on; others: "PLAYER N LEFT".
   Closing mid-match: the AI takes the player. Host leaves: the longest in the party hosts.
C. Connection lost: "CONNECTION LOST: YOU PLAY ON YOUR OWN" (no restart).
D. Joining during a match: "THE PARTY IS IN A MATCH: YOU JOIN AFTER IT".

## Rules per player P (no special cases per mix)
1. Camera: as solo, from P's team's end (no team: the game's camera with no human side); P's style.
2. Control: one player of P's team at a time; never the one another human of the team controls (the
   game's own rule, measured first).
3. Markers: every human's controlled player with that human's marker on every screen (the game's).
4. Decisions: the controller the game gives a set piece to sees the menu; everyone else sees the game
   carry on (as the AI deciding) and "PLAYER N IS CHOOSING".
5. Pause: shared, 3 a match per player, 60 s.

## Steps (features.json; each passes only by its check)
1. ls.party, ls.leave, aems.rebase: what is built, proven (aems.rebase PASS).
2. ls.rules: measure the game's own rules: control with several humans on a team, who gets a decision.
3. ls.own_view: rule 1 (det_local camera step for P's team), any mix, sync proven.
4. ls.decisions: rule 4, any mix.
5. ls.lag: simulated internet delay and jitter, no stalls, minimum input delay.
6. ls.fastjoin: faster catch-up (indirect-call lookup, TLS, ...), then a save-state join if reliable.
7. ls.mixes: every mix of up to 4 players stays in sync (native copies).
Local only; the user deploys the web app later.

## Open (2026-10-14, measured)
- End camera flip: every flip follows camera event 0xAD (possession changed), its data the side that
  gained the ball; hiding the other team's controls (+0xC0) in the camera's event/step changed nothing
  (own_end.c v1). Next: in each copy the camera does not hear an 0xAD for the other team (as solo).
- Pause did not open (friend's Start at frame 20729, Esc pressed in both games) in the run with own_end v1
  and the END ON camera, 96 frames after a flip; worked in every run without them. Must be cleared by the
  next full ls_match run (pause step) or isolated per camera.
- 2 of 194 pictures differed (bottom-left HUD blocks, one frame each) while the match was equal.
- own_end v2 (each copy's camera skips 0xAD for another team's player; camera listener and step in
  det_local sections; the C library's rand seed and the copy protection's countdowns, [0x01409500,
  0x0140AA80), put back after the camera): scheduling equal (21172 frames), but the copies still part
  (ls_match_check: 2 of 133 hashes; ls_own_view: friend DESYNC at frame 17279, after "WebGL: too many
  errors"). Measured on the way: the camera never moves rand's seed (solo, whole match); its writes are
  its own objects, the stack and three countdowns (0x01409630/0x01409720/0x01409724: FUN_0042A360, a
  protected getter, from FUN_006E8500 / FUN_006E8180, watchpoint). Switched off (RT_OWN_END=1 in checks
  only) until ls.own_view passes. Next: RT_MS_CAMWRITES in a party with the cameras apart, and the WebGL
  errors.
- DONE 2026-10-14: rule 1 (ls.own_view PASS) drawn only; the one-frame "HUD" picture difference was the
  previous frame's "YOU: PLAYER N" overlay left in the back buffer where the game did not draw again
  (RT_PIX_STRIP): overlay.cpp keeps the game's frame under the overlay and puts it back after the swap.
  ls.match, ls.cursor PASS.
- ls.lag (2026-10-14): delay 9 -> 6 frames (adaptive), 0 stalls; peer share 86-89 % (wants 90 %): in the
  check both pages and the relay run on one machine (relay hop ~0 ms, 52-54 fps); to confirm on two
  machines (the user's test), not by easing the check.
- ls.fastjoin (2026-10-14, measured): catch-up is the game's own code (native profile: lookup 4.5 %, TLS
  16 % native only, scheduler 4 %); -O2 for the lifted code: 60 -> 56 s for 16000 frames, state identical
  (53 hashes): not worth a web build's time (RECOMP_GEN_OPT kept, default -O1). Joining replays the room's
  session: ~1/3 of its length in the browser (party made first: ~1.5 min). A save-state join is not
  reliable here: guest threads are native threads (their state on native stacks) and D3D/audio objects
  live outside guest memory.
