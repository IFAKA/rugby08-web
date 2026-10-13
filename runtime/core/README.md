# runtime/core/ — boot and the machine the lifted code runs on

- `main.c` boot: arena, PE image, imports, main thread, the game's entry point.
- `cpu.c` CPU state globals, dispatch lookup, import/COM bridges, the guest lock, `guest_call`, dispatch misses.
- `cpu_ext.c` helpers for instructions `tools/lift_ext.py` adds (faults, BCD, packed BCD).
- `mem.c` guest arena + PE loader · `heap.c` guest heap (dlmalloc mspace) · `thread.c` per-thread CPU state.
- `det.c` deterministic mode (`RT_DET=1`: virtual clock, guest scheduler, state hash) · `pads.c` virtual pads.
- `interp.c` (+`interp_int.h`) Unicorn fallback for guest code without a lifted body.
- `fe_pointer.c` the browser: the menus' cursor placed where the pointer is (every Present).
- `log.c` `rt_log`/`rt_trace`/`rt_fatal` · `trace.c` function-entry ring and crash dump · `aems.c` sound-bank code lookup · `png.c` PNG writer.
