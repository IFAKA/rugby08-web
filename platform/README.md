# platform/ — what differs between hosts

`runtime/include/plat.h` is the interface. `sdl/` implements it for both targets (window, GL context,
events, keys, replay, audio); `native/` and `web/` add what only one host has.
