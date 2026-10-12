# Playing Rugby 08 online

Online is part of the game: everyone plays in their own game, in their own menus, and when the host
starts a match the friends' games go into it. One game, the host's, plays the match; the friends see it
and play their players with their own controllers. Up to 4 players, the game's own limit.

## How it works
- The host's game plays the match. Each friend's game sends its pad to the host, where it is one more
  controller (Team Select gives it a side, as on a couch), and shows the host's match as it happens: the
  host sends every match frame, compressed by the game's own replay coder (~380 bytes a frame).
- When the host starts a match, every friend's game goes into it from whatever menu it is in, with the
  host's teams, kits and stadium; when the host's match ends, everyone is back in their own menus.
- Pausing: a friend's Esc pauses the host's match and everyone's; whoever paused drives the pause menu.
- Nothing is replayed to join: a friend can join at any time, even during a match.
- Code: `runtime/core/net_ha.c` (messages), `net_ha_flow.c` (the party), `match_puppet.c` (a friend's
  game showing the host's match), `ui_online.c` (the screens); design in docs/MATCH_STATE.md and
  docs/plans/2026-10-12-online-host-authoritative.md.

## In the game
- **F2** opens **ONLINE**: **HOST A GAME** gives your room code; **JOIN A GAME**: type a friend's code,
  Return. F2 again closes the screens; a corner badge shows the room and how many are in it. In the room
  screen, **LEAVE** goes back to playing alone. Keys pressed while the screens are open never reach the
  game.
- Then everyone moves their controller to a side in the host's **Team Select**, like on a couch.
- Friends answer their side's set-piece prompts with their pad (the host's match waits for them).
- Every game has the Online screens, the browser version and the Mac app alike. The game finds its
  controllers once, when it starts, so friends' controllers exist from the start: Team Select shows
  three idle pad icons even when you play alone.

## Run it on your machine (testing)
```sh
./init.sh build-web && ./init.sh build-native   # the browser build (build/web) and the Mac app
cd server && go run . -addr :8090 -dir ../build/web
```
Open http://localhost:8090 in two windows (or one window and `build/native/rugby08 --net
ws://127.0.0.1:8090/ws`): F2 → HOST A GAME in one, F2 → JOIN A GAME and the code in the other.

## Run it for your friends (your own server)
Browsers only run this game over **HTTPS** (or on localhost), so the server needs a certificate for
your domain (Let's Encrypt, or one you have):
```sh
go build -o rugby08-server ./server
./rugby08-server -addr :443 -dir build/web -password "something-only-your-friends-know" \
  -cert /etc/letsencrypt/live/YOUR.DOMAIN/fullchain.pem -key /etc/letsencrypt/live/YOUR.DOMAIN/privkey.pem
```
Upload `build/web/` (including `build/web/data/`, the packaged game files) next to the server binary.
Friends open your address and type the password once. Nothing else is contacted: the page, the game
data and the game traffic all come from your server.

## Limits
- The Mac app talks plain `ws://` only: against an HTTPS server it cannot connect yet (it needs TLS:
  a library such as OpenSSL), so for now it plays online on a local network, or everyone uses browsers.
- A friend's game shows the host's cutscenes as a held picture (the host's replays and celebrations are
  not sent yet); half time and full time have been checked only through a quit.
- The older lockstep mode (every game plays the whole match from the presses) is still in the page
  behind `?panel`, for its checks.
