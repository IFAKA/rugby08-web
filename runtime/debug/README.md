# runtime/debug/ — opt-in debugging and check tools

Each is armed by an environment variable or a `tools/hooks.json` name and costs a branch otherwise.

- `probes.c` RT_PROBE, RT_PROBE_DUMP, RT_DUMP · `coverage.c` RT_COVERAGE (and the per-Present frame hook).
- `hooks.c` hooks.json names (`NAME entered`, args:, list:, ctl:, stick:, shadow:) · `selftest_crt.c` RT_SELFTEST_CRT.
- `shadow.c` the shadow check (interpreter vs lifted code) · `frames.cpp` per-Present: `--frames`/`--screenshot`,
  replay shots, the menu-change probe, RT_TRACE_DRAWS_FRAME, RT_LOG_LIGHTS.
- `match_probe.c` RT_MS_SCAN / RT_MS_DUMP / RT_MS_SNAP (finding match objects) · `match_stream_check.c`
  RT_MS_STREAM_CHECK (a host's replay records decoded by this game) · `net_ha_press.c` RT_NET_HA_PRESS (a
  friend's test input in the host's open play) · `match_quit.c` RT_MS_QUIT_AT (quit each match at a match time).
