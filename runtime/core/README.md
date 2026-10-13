# runtime/core/ — boot and the machine the lifted code runs on

- `main.c` boot: arena, PE image, imports, main thread, the game's entry point.
- `cpu.c` CPU state globals, dispatch lookup, import/COM bridges, the guest lock, `guest_call`, dispatch misses.
- `cpu_ext.c` helpers for instructions `tools/lift_ext.py` adds (faults, BCD, packed BCD).
- `mem.c` guest arena + PE loader · `heap.c` guest heap (dlmalloc mspace) · `thread.c` per-thread CPU state.
- `det.c` deterministic mode (`RT_DET=1`: virtual clock, guest scheduler) · `det_hash.c` its state hash · `pads.c` pad slots (this machine's game controller, the party's friends) · `pad_attach.c` the game's controllers follow them (plugged in or not, entries 1..4).
- Online: `netplay.c` (+`netplay_int.h`) lockstep messages, roster and session · `net_frame.c` the per-frame step ·
  `net_join.c` joining a room from the command line · `net_ha.c` host-authoritative play (`--net-mode ha`: friends'
  pads to the host, the host's frames to friends; `net_ha_rx.c` a friend's frames back in order) · `match_state.c` the live match in guest memory (docs/MATCH_STATE.md) ·
  `match_setup.c` the match definition at launch · `match_replay.c` the game's replay frames and records (what a host
  sends) · `match_puppet.c` a friend's game showing the host's frames · `match_camera.c` the match view ·
  `match_measure.c` RT_MS_POS / RT_MS_SHOT_T numbers for tools/net/match_compare.py · `net_ha_flow.c` the party
  following the host (launch, intro, end, pause).
- The party in the game's own menus (pt.menu): `fe_xml.c` its screen files as a tree (binary .rfe in, XML out) ·
  `fe_screens.c` ONLINE in the main menu and the party's screens · `fe_party.c` what they do (keys, texts, the
  menus' cursor at the browser's pointer) · `ui_lobby.c` parties on the relay · `ui_badge.c` the party badge
  (`ui_font.c` bitmap font, drawn by `render/webgl/overlay.cpp` at Present).
- `interp.c` (+`interp_int.h`) Unicorn fallback for guest code without a lifted body.
- `log.c` `rt_log`/`rt_trace`/`rt_fatal` · `trace.c` function-entry ring and crash dump · `aems.c` sound-bank code lookup · `png.c` PNG writer.
