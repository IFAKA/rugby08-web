# runtime/win32/ — the Win32 API the game imports

One `HLE(Name)` handler per import (`tools/gen_imports.py` binds them by name; an import without one
logs `UNIMPL`). Shared helpers: `win32.h` (handles, paths, strings, time).

- kernel32: `k32_sys.c` (system, modules, environment, console, process), `k32_locale.c`, `k32_time.c`,
  `k32_file.c` (files, find, mappings), `k32_mem.c`, `k32_sync.c` (sync objects, TLS, threads), `k32_eh.c` (SEH).
- user32: `user32_window.c` (classes, windows), `user32_geom.c`, `user32_msg.c` (queue, dispatch), `user32_input.c`
  (keyboard, cursor, metrics); shared: `user32_int.h`.
- `winmm.c` timers · `advapi32.c` registry · `ddraw.c` · `dinput.c` DirectInput 7 · `dsound.c` DirectSound mixer.
- `handles.c` kernel object table · `paths.c` guest paths → `Game Files/` and the user dir · `strings.c`.
