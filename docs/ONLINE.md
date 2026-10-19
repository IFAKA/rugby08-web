# Playing Rugby 08 online

Online is part of the game, as in sports games' online modes: every player runs their own game and
every game plays the same match from everyone's button presses (lockstep). So every screen shows
everything the game does (penalty options, kick aim, banners, cutscenes, replays, the referee), and
each player drives their own controller. Up to 4 players, the game's own limit.

## How it works
- Every game is a session from the moment it starts, with its room open on the server (offline it is
  simply the plain game). Only presses travel; each game simulates everything itself, in deterministic
  mode (runtime/core/det.c), and the games compare state hashes every 2 s.
- Joining: the friend's game is rebuilt by replaying the room's presses from the start, fast and
  without drawing (a "JOINING THE PARTY" screen with its progress), then it plays along. Nobody waits
  for it meanwhile: the server tells it where the others are and it sends empty presses until it is in.
- Leaving, or a connection that stops for 3 s: that player's controller is unplugged at the same frame
  in every game and the AI takes their player; the others play on (the host leaving too).
- Data a game reads from its own machine rather than from presses (the stadium shadow map it draws and
  reads back) is read by one game and shared ('X', runtime/core/net_external.c).
- Code: runtime/core/netplay.c, net_frame.c, net_external.c, ls_party.c (the party screens),
  server/main.go; plan: docs/plans/2026-10-13-lockstep-party.md.

## In the game
- Main menu **ONLINE** > **CREATE PARTY** shows your party's code. **JOIN PARTY**: type or pick your
  friend's code; your game becomes your friend's (it catches up first). **LEAVE PARTY**: back to your
  own game. Messages ("NO PARTY WITH THAT CODE", "PLAYER 2 JOINED", a lost connection) appear in the game.
- Everyone shares the menus, as on one couch. In **Team Select** each player moves their own controller
  to a side. The host plays with keyboard and mouse (controller 1) or a game controller; friends with
  their keyboard (arrows, S, A, D, Space, Esc...) or a game controller.
- Pause: Start / Esc pauses for everyone.

## Run it on your machine (testing)
```sh
./init.sh build-web
cd server && go run . -addr :8090 -dir ../build/web
```
Open http://localhost:8090 in two windows: ONLINE > CREATE PARTY in one, ONLINE > JOIN PARTY and the
code in the other. Every game's log goes to `logs/play/` (one file per page load) for reading back.

## The party site (for your friends: private, each player brings their own game)
Decided with the user (2026-10-19): one website that any friend opens from the party link, so anyone
can start a party, with nobody's computer needing to stay on. The site serves the program, never the
game's art, sound or video: each player picks their own Rugby 08 folder once (platform/web/js/own_copy.js;
every 8 MiB piece is checked against the site's manifest, then kept in that browser).
```sh
./init.sh build-web && .venv/bin/python tools/web/site_manifest.py build/web build/site
go build -o rugby08-server ./server
./rugby08-server -addr :443 -dir build/site -stun 3478 -password "a-long-party-password" \
  -cert /etc/letsencrypt/live/YOUR.DOMAIN/fullchain.pem -key /etc/letsencrypt/live/YOUR.DOMAIN/privkey.pem
```
The party link is `https://YOUR.DOMAIN/k/a-long-party-password`: send it to your friends. Without
it the site only says it is private (server/gate.go). Without `-password` the site is public (meant only
for after the EA parts are replaced by a clean-room version). Browsers run the game only over **HTTPS**.

## For players (what to tell your friends)
1. You need a computer with Chrome or Edge, your own Rugby 08 installed (the same version as everyone's),
   and about 2 GB of free disk space.
2. Open the party link a friend sent you.
3. The first time only: press **Choose my Rugby 08 folder** and pick the folder you installed the game
   in (the one with the Audio and Data folders). The page checks the files and keeps them in your
   browser; nothing is uploaded. Next time the game starts straight away.
4. In the game: **ONLINE** > **CREATE PARTY** and tell your friends the code, or **ONLINE** >
   **JOIN PARTY** and type a friend's code.

## Limits
- Joining takes longer the longer the party's session has run (the game replays it at ~4x speed).
- Browsers only (the Mac app talks plain ws:// and is a test tool).
