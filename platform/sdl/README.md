# platform/sdl/ — plat.h on SDL2 (native and web)

`sdl_window.c` window, GL context, cursor · `sdl_events.c` SDL events → Win32 messages · `sdl_keys.c` key maps
and state · `sdl_replay.c` input replay (`--replay`, tests/replays/) · `sdl_audio.c` audio output · `sdl_skip.c` which steps a slow device leaves undrawn (fps.skip; both pacers) · `sdl_desk.c` netplay keyboard and mouse (recorded on the host, injected
everywhere) · `sdl_pad.c` this player's live pad (controller or keyboard) · shared: `sdl_int.h`.
