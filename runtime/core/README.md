# runtime/core/ — boot and the machine the lifted code runs on

- `main.c` boot: arena, PE image, imports, main thread, the game's entry point.
- `cpu.c` CPU state globals, dispatch lookup, import/COM bridges, the guest lock, `guest_call`, dispatch misses.
- `cpu_ext.c` helpers for instructions `tools/lift_ext.py` adds (faults, BCD, packed BCD).
- `mem.c` guest arena + PE loader · `heap.c` guest heap (dlmalloc mspace) · `thread.c` per-thread CPU state.
- `det.c` deterministic mode (`RT_DET=1`: virtual clock, guest scheduler) · `det_hash.c` its state hash · `pads.c` virtual pads.
- Online: `netplay.c` (+`netplay_int.h`) lockstep messages, roster and session · `net_frame.c` the per-frame step ·
  `net_join.c` joining a room from the command line · `match_state.c` the live match in guest memory (docs/MATCH_STATE.md).
- `interp.c` (+`interp_int.h`) Unicorn fallback for guest code without a lifted body.
- `log.c` `rt_log`/`rt_trace`/`rt_fatal` · `trace.c` function-entry ring and crash dump · `aems.c` sound-bank code lookup · `png.c` PNG writer.
