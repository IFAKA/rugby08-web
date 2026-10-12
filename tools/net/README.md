# tools/net/ — online play checks

Each prints one summary line; details in `logs/`. `net_test.sh` runs two native games over the relay
(`server/`), `net_web_test.sh` / `net_dropin.py` / `net_live_input.py` do the same in headless Chromium
(Playwright, system Python), `net_handoff.sh` checks that a room outlives its creator, `pov_check.sh` the
per-player camera. `match_*_check.sh` are the host-authoritative items (`on.*` in features.json, plan in
docs/plans/2026-10-12-online-host-authoritative.md). `match_compare.py HOST.out FRIEND.out` lines two
games' `ms: pos` lines (RT_MS_POS, runtime/core/match_measure.c) up by match time and reports where they
first differ, including what a friend's own recorder wrote against the host frame. `match_net_check.sh`
runs a host and a friend in host-authoritative mode over a local relay (`--net-mode ha`); `match_flow_check.sh`
a party through two matches (join from the menus, pause from the friend, the ends); `ingame_ui_check.sh` a room
made and joined from the in-game Online screens by a native and a browser friend (`ingame_ui_web.py`, Playwright).
