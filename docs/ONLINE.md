# Playing Rugby 08 online

Everyone plays in their own browser (Chrome, Edge, Firefox 153+ or Safari 27+). Each player keeps
their own controls (keyboard or gamepad) and their own camera; the match itself is shared.

## How it works (one paragraph)
Every player's browser runs the whole game. The game runs in a deterministic mode (`runtime/core/det.c`)
where time, threads and input are controlled so that the same inputs always give the same match. Only
button presses travel between players, through your own relay server (`server/`). Every two seconds
the browsers compare a fingerprint of the match; if they ever disagree the match stops and tells you
("Out of sync"). Up to 4 players (the game's own limit).

## Run it on your machine (testing)
```sh
./init.sh build-web                         # the browser build (build/web)
cd server && go run . -addr :8090 -dir ../build/web
```
Open http://localhost:8090 in two browser windows: **Play online with friends → Host a game** in one,
paste the invite link (or type the code) in the other, then **Start match**.

## Run it for your friends (your own server)
Browsers only run this game over **HTTPS** (or on localhost), so the server needs a certificate for
your domain (Let's Encrypt, or one you have):
```sh
go build -o rugby08-server ./server
./rugby08-server -addr :443 -dir build/web -password "something-only-your-friends-know" \
  -cert /etc/letsencrypt/live/YOUR.DOMAIN/fullchain.pem -key /etc/letsencrypt/live/YOUR.DOMAIN/privkey.pem
```
Upload `build/web/` (including `build/web/data/`, the packaged game files) next to the server binary.
Friends open `https://YOUR.DOMAIN`, type the password once, and use the invite link you send them.
Nothing else is contacted: the page, the game data and the match traffic all come from your server.

## In the game
- Controls: arrows run · S accept / pass · A/D pass left/right · Space sprint · Esc pause · or a gamepad.
- **C** changes *your* camera (Game camera, Closer, Wider, Higher, Lower, Overhead). Only you see it.
- Pausing (Esc / Start) pauses everyone, like a couch game.
- If someone's connection stalls, the game waits and shows "Waiting for the other players…".

## Limits (honest)
- Online is browser-to-browser. The native Mac app can play online with another native app
  (`--net ws://HOST/ws --net-room CODE --net-players 2`), but not against a browser.
- The camera presets re-draw the game's own view; the game only draws what its camera sees, so the
  presets stay close to it (very wide views can miss distant objects).
- A player who disconnects ends the match (there is no rejoin into a running match).

## Checks (features.json)
net.det_synctest, net.det_chaos, net.lockstep, net.desync_detect, net.web_lockstep, net.pov_view,
net.pov_independent, net.live_input.
