# Playing Rugby 08 online

Online is part of the game, like a console party: you open the game and play; friends join *your*
game whenever they want, as extra controllers, and play whatever you pick in the game's own menus
(any mode the game has for several controllers). Up to 4 players, the game's own limit. Each player
keeps their own controls (keyboard or gamepad) and their own camera.

## How it works
Every player runs their own complete copy of the game, in their browser. Only button presses travel,
through your own server (`server/`). The game runs in a deterministic mode (`runtime/core/det.c`):
the same presses always give the same game, so every copy shows the same match.

A friend who joins later gets the list of every press since you opened the game and their copy
replays it at full speed, without drawing or sound, until it reaches the present. Your game waits a
moment meanwhile ("Brother is joining… 40%"). That replay is why **joining takes longer the longer you
have already played**: right after you open the game it takes seconds; after a long match, minutes.
Invite friends first, then pick a mode.

The copies compare a fingerprint of the game every 2 seconds. A copy that ever disagrees rebuilds
itself by joining again, automatically. If your connection drops, your game carries on offline.

## Run it on your machine (testing)
```sh
./init.sh build-web                         # the browser build (build/web)
cd server && go run . -addr :8090 -dir ../build/web
```
Open http://localhost:8090. The game starts at once. Click **👥 Friends** (top right) → **Copy** the
invite link → open it in another window: that window joins as controller 2.

## Run it for your friends (your own server)
Browsers only run this game over **HTTPS** (or on localhost), so the server needs a certificate for
your domain (Let's Encrypt, or one you have):
```sh
go build -o rugby08-server ./server
./rugby08-server -addr :443 -dir build/web -password "something-only-your-friends-know" \
  -cert /etc/letsencrypt/live/YOUR.DOMAIN/fullchain.pem -key /etc/letsencrypt/live/YOUR.DOMAIN/privkey.pem
```
Upload `build/web/` (including `build/web/data/`, the packaged game files) next to the server binary.
Friends open your invite link and type the password once. Nothing else is contacted: the page, the
game data and the game traffic all come from your server.

## In the game
- **👥 Friends** (top right): your invite link, who is playing (controller number, ping), your name.
- Controls: arrows run · S accept / pass · A/D pass left/right · Space sprint · Esc pause · or a gamepad.
- Each friend is one more controller: in the game's **Team Select** everyone moves their controller
  to a side, exactly like on a couch. Anyone can use the menus, like on a couch.
- **C** changes *your* camera (Game camera, Closer, Wider, Higher, Lower, Overhead). Only you see it.
- Game settings (difficulty, match length…) are part of the game, so they are the host's game's.
- Pausing (Esc / Start) pauses everyone, like a couch game.
- A friend who leaves just unplugs their controller; the game goes on. If you (the one who opened
  the game) leave, your friends keep playing and can still invite others.

## Limits
- Joining replays everything since the game was opened (see above): invite early.
- Everyone should use the browser version (any modern browser). The native Mac app can play online
  with other native apps (`--net ws://HOST/ws --net-room CODE`), but not with browsers.
- The camera presets re-draw the game's own view; the game only draws what its camera sees, so the
  presets stay close to it (very wide views can miss distant objects).

## Checks (features.json)
net.det_synctest, net.det_chaos, net.lockstep, net.desync_detect, net.web_lockstep, net.pov_view,
net.pov_independent, net.live_input, net.dropin, net.handoff, net.web_dropin.
