# Online party: the four bugs of the 2026-10-14 play test, causes measured, and the fixes

The user's play test (two browser windows, one party): (1) LEAVE PARTY restarts the game (EA intro),
(2) the friend is removed ("expulsado") when the match starts, (3) rejoining resets the friend's game,
(4) the host's game freezes after QUIT MATCH. Goal: an online mode that behaves like online sports games
(EA FC / FIFA friendlies), for friends now and for anyone on other networks later: the same code path.

## What professional games do (research)
- Xbox certification XR-074: when a multiplayer session is lost, the title tells the player, ends the
  session and returns to the main menu; no message is a failure. No restart, no crash.
- EA FC / FIFA online friendlies: if the other player leaves, you get a message and go back to the menus;
  your own game keeps running. Leaving a party takes you to the menu, never reboots the game.
- Rugby 08 (PC/PS2) had no online play of its own: we follow the sports-game convention above.

## Bug 1: LEAVE PARTY restarts the game
- Cause: deliberate. `platform/web/shell.html` `rugbyLeave()` = `reload('')` (a page reload: the game
  boots again). Same for a lost connection (`rugbyNetStatus` kind 3) and exit code 4.
- Why it was done: every game of a party runs the party's whole session from frame 0 (lockstep); a player
  leaving still has the party's game state, so the page just started a new one.
- Fix: leave in place. The leaver's game marks the others as gone (the same roster event 'D' the others
  get for it), takes over the menus as the only player (desk = lowest active slot, `ls_host`), and goes to
  the main menu, or out of a match the way the pause menu's QUIT does (`party_leave.c`: FE return =
  main menu, 0x576140). An in-game message says what happened. One path for: LEAVE PARTY, the friend
  leaving, the connection lost.

## Bug 2: the friend is removed when the match starts
- Measured (headless two-player runs, `RT_DET_HASH`, `RT_DET_PAGES`, `RT_DET_DUMP`):
  - the user's rooms NJ4JTM and QWNLP2 and 4 of 4 repro runs: a desync within ~30 frames of the match
    starting; the load timer read differs (6.357862 vs 6.358262 s), the kick-off is mirrored, a different
    tunnel video (dunedin01 vs 02): the two games loaded the match differently.
  - the games' virtual clocks are equal up to the match and split ~10-15 frames into its loading (+0.5 ms).
  - the full memory of the two games differs from frame ~5430, the CREATE PARTY screen; the only page
    that differs is 0x02560000, and the only bytes that differ are the party rows' text:
    host "PLAYER 1 (HOST) (YOU)" / "2  WAITING...", friend "PLAYER 1 (HOST)      " / "2  PLAYER 2 (YOU)".
- Cause: "(YOU)" was written into the game's own memory, different in each game. The game draws different
  text (different code runs), the lockstep clock (`det.c`: it counts the game's work) drifts, and the
  match loading, which reads that clock, comes out different. The desync check then removes the friend.
- Fix: the game's rows have the same text in every game; "YOU" is drawn over this player's row by the
  runtime (`ui_badge.c`), never in the game. Rule: nothing per player is ever written into the game.
- Same check for the own camera and own cursor (per player by design): measure the full memory hash and
  clock with them on; if they change the game, they move to drawing only.
- Proof required: full memory hash and clock equal in both games through the menus, the loading and the
  match; `ls_match_check` passes 3 times in a row.

## Bug 3: rejoining resets the friend's game
- Measured (rooms NJ4JTM, QWNLP2): the joiner replays the party's log; the log has the slot's previous
  player (the same friend, earlier) joining ('A' at 6769) and leaving ('D' at 15604). `netplay.c` takes the
  old 'A' as "I joined at 6769", then the old 'D' as "I was removed": exit 4, "YOU LEFT THE PARTY", reload.
- Fix: the server tells the joiner where its own entry starts; older 'A'/'D' for its slot are someone
  else's (still applied to the roster, never to "me").

## Bug 4: the host freezes after QUIT MATCH
- Measured: "ICALL: unresolved VA 0x0265758F from 0x007D0470" right after the game re-read
  fe_sfx.abk and apofloop.abk (frames 9994-9996), then an out-of-bounds access on the audio thread.
- Cause: EA's sound banks hold code; the game loads them into the heap. `runtime/core/aems.c` records a
  bank's address the first time and never again, so a bank reloaded at a new address (after a match) is
  not found: the sound thread calls nothing and dies. Not online-specific: any QUIT can hit it.
- Fix: a bank loaded at a new address is found there too. Proof: a test with a bank at two addresses,
  and a long match then QUIT.

## Also
- A real desync (should not happen once the above holds): "RESYNCING WITH THE PARTY" instead of a kick.
- Limit kept for later: joining replays the party from its start (~40-80 s); strangers/matchmaking need
  joining from a saved state instead. The server is localhost only until the user says to put it online.

## Status (2026-10-14, measured)
- Bug 2, "(YOU)": fixed (fe_party.c writes the same rows in every game; ui_badge.c draws "YOU"). Two-player
  match run: both games enter the match at the same frame, 160-169 of 169 match hashes equal, the friend is
  no longer removed. Still open: in one of two runs the virtual clock split 25 frames into the match
  loading (0.5 ms, frame 16440) with the match itself still equal; being traced (RT_DET_SCHED).
- A friend's picture: one flat frame while loading the match (the browser cleared the canvas while the
  game waited mid-frame for the host's shadow-map bytes). Fixed: the canvas keeps its drawing
  (preserveDrawingBuffer, render/webgl/factory.cpp).
- Bug 3 (rejoin): the server sends "since" in "joined"; netplay.c takes a slot's 'A'/'D' as its own only
  from there. Bug 4 (QUIT freeze): aems.c moves a bank to where it is now; tools/check/quit_sound_check.sh
  (aems.rebase) passes: match, QUIT, fe_sfx.abk found at 0x026B7F80, 6000 frames of menus, no errors.
- Bug 1 (LEAVE PARTY): server/fork.go (+ fork_test.go, passes) and runtime/core/net_leave.c: the leaver's
  game plays on in a room of its own, at its main menu; the host is the player in the party the longest;
  the keyboard and mouse follow the host in the menus. Lost connection: plays on alone (no reload).
  tools/net/ls_party_check.py checks it (leave in place, a newcomer in sync in the leaver's room, the leaver
  joining the party again).
- Found by RT_DET_SCHED (det.c: each frame's scheduling totals; each time step with its thread):
  1. A false "out of sync": ls_simhash hashed the ball struct whole, 2 bytes of padding included (whatever
     the stack held, per machine): 18 of 175 match hashes differed with identical games (all 276 position
     samples, memory and clock equal). Without the test's keep-going switch that kicks the friend at random
     moments of a match. Fixed: field by field.
  2. The friend's own pointer was written into the game's cursor while the game drew (ls_cursor.c):
     the drawing ran differently, the lockstep's thread switches moved (1550 frames, only in the menus
     while the host chose and the friend watched; identical through the whole match). That is how the
     match loading could come out different (run 7). Fixed: the game always draws the shared cursor; a
     friend's GPU draws that quad moved to the friend's pointer (world transform, d3d8_device.cpp).
  3. The own camera: scheduling identical through the whole match (default camera): no effect on the
     shared game.
