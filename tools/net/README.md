# tools/net/ — online play checks

Each prints one summary line; details in `logs/`. `net_test.sh` runs two native games over the relay
(`server/`), `net_web_test.sh` / `net_dropin.py` / `net_live_input.py` do the same in headless Chromium
(Playwright, system Python), `net_handoff.sh` checks that a room outlives its creator, `pov_check.sh` the
per-player camera. `match_*_check.sh` are the host-authoritative items (`on.*` in features.json, plan in
docs/plans/2026-10-12-online-host-authoritative.md). `match_compare.py HOST.out FRIEND.out` lines two
games' `ms: pos` lines (RT_MS_POS, runtime/core/match_measure.c) up by match time and reports where they
first differ, including what a friend's own recorder wrote against the host frame. `match_net_check.sh`
runs a host and a friend in host-authoritative mode over a local relay (`--net-mode ha`); `match_flow_check.sh`
a party through two matches (join from the menus, pause from the friend, the ends).

Browser party checks (headless Chrome with the real GPU, muted; system Python /opt/homebrew/bin/python3):
`web_route.py` drives the menus by the traced front-end screen id, `web_party.py` the players (a page each, the
relay on a free port, the Online screens). `web_soak.py` (pt.freeze: two pages in a party match for 10 minutes,
the host's tab hidden), `party_controllers_check.py` (pt.controllers: solo = the real devices, friends add and
remove controllers, four sides), `party_menu_check.py` (pt.menu: ONLINE in the game's menus by keyboard, mouse and
pad), `party_webrtc_check.py` (pt.webrtc: data channels, losses, latency vs the relay).
Lockstep party (ls.*): `ls_boot_check.py`, `ls_party_check.py`, `ls_controls_check.py`, `ls_match_check.py` (a party
match: equal state hashes and pictures (RT_PIX), the friend's Start pauses both, its own camera changes only its
picture); `pix_dump.py` turns RT_PIX_DUMP frames of two players into PNGs and a difference image.
`ls_joinview_check.py`: joining shows the party screen from JOIN PARTY on (never a black one), "(YOU)" on each
game's own row, equal hashes.
