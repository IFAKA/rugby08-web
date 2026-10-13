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

## Run it for your friends (your own server)
Browsers only run this game over **HTTPS** (or on localhost), so the server needs a certificate for
your domain (Let's Encrypt, or one you have):
```sh
go build -o rugby08-server ./server
./rugby08-server -addr :443 -dir build/web -password "something-only-your-friends-know" \
  -cert /etc/letsencrypt/live/YOUR.DOMAIN/fullchain.pem -key /etc/letsencrypt/live/YOUR.DOMAIN/privkey.pem
```
Upload `build/web/` (including `build/web/data/`, the packaged game files) next to the server binary.
Friends open your address and type the password once. Nothing else is contacted.

## Limits
- Joining takes longer the longer the party's session has run (the game replays it at ~4x speed).
- The camera is the game's, shared (in lockstep it is part of the game; a personal view is to come).
- Browsers only (the Mac app talks plain ws:// and is a test tool).
