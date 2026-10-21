# Render thread (fps.interp on fast screens)

## Goal and done
- A 120/144 Hz screen shows in-between frames while the game steps 60 times a second (fps.interp's
  check: median >= 70 in-between frames a second at 144 Hz, cameras between the steps' cameras).
- Nothing the game sees changes: deterministic runs keep every state hash and every screenshot
  (`tools/check/regress.sh compare`, pixel for pixel), fps.skip and web.smoke still pass.
- Native (macOS, then Windows/Linux) and the browser.

## Why a thread
Measured serially (fps branch, after the speedups): a step costs ~8 ms, a 144 Hz refresh is 6.9 ms.
An in-between frame has to wait a full refresh after the step's own frame, so no serial order
fits two of them per step, however fast the step gets. The drawing has to run beside the game.

## Prior art
- Wine's wined3d CSMT (Stefan Dösinger 2013, upstream in Wine 2.6, 2017): every D3D call is a
  command in a stream run by one GL thread; calls that return data (maps) wait for the stream,
  DISCARD maps get fresh memory instead (Comminos, "Approaching One Driver Overhead", 2018).
- DXVK's CS thread (the same for D3D9/11 on Vulkan); Mesa glthread (GL calls marshalled to a
  worker); Nvidia's __GL_THREADED_OPTIMIZATIONS.
- Emulators: RT64 (N64 recomps: the game thread records, the renderer draws and interpolates),
  Dolphin dual core, PCSX2 MTGS.
- Browser: Emscripten `-sOFFSCREENCANVAS_SUPPORT` (a pthread owns the WebGL context, the canvas
  handed over at pthread_create with emscripten_pthread_attr_settransferredcanvases;
  `-sOFFSCREEN_FRAMEBUFFER` as the proxied fallback).

## Design (one)
The queue sits where wined3d's does: at our D3D9 device (render/webgl), not at GL. The game's
D3D8 layer calls ~40 of its entry points; GL has 90 functions at ~400 call sites.
- Game thread: runtime/d3d8 calls a device whose methods post a closure to a single-producer,
  single-consumer queue and return. Buffer and texture locks already write CPU memory
  (GLBufferStore::lock, D3D9Texture::LockRect); Unlock posts the upload owning that memory.
- Render thread: owns the GL context; runs the closures in order, Present swaps. Between two
  Presents, at each refresh, it replays the last step blended (interp.cpp as now).
- Calls that return data wait for the queue to drain (rare: Create*, GetRenderTarget data,
  render-target locks, screenshots, the party view's frame read). Same results, so the same
  guest state.
- The game runs at most one step ahead of the drawing (Present waits if two are queued).
- Window events stay on the main thread (SDL); natively the render thread makes the context
  current (SDL_GL_MakeCurrent).

## Gaps (to fill in order)
1. Calls that return data. Found: GL outside render/webgl is only d3d8_levels.cpp's render-target
   readback (gpu_read: the stadium's 256x256 shadow map, read back by the game), runtime/debug
   frames.cpp (screenshots), frame.c's swap + glFinish; d3d9w_frame_read (party view),
   d3d9w_cost_take (stats). Shaders: d3d9w_progvs/ps_create return objects (create game-side,
   compile on the render thread). Open: how often the shadow-map readback runs in a match (a wait
   per step costs overlap, not correctness).
2. Native: a GL context current on another thread under SDL on macOS (Metal-backed GL).
3. Browser: the WebGL context on a pthread (OffscreenCanvas), its pacing (rAF in the worker, or
   explicit swap) and the fallback when OffscreenCanvas is missing. The web build already uses
   pthreads (-pthread, PTHREAD_POOL_SIZE=16, COOP/COEP).
4. Queue memory: a step's closures and uploads (a match frame's buffer uploads) without
   allocating per call.

## Filled (native, 2026-10-21; platform/native/render_thread.c, render/webgl/queue.cpp)
- Gap 1: readbacks (gpu_read, screenshots, frame read) run through `d3d9w_on_render`, which waits
  for the queue to drain. Regress with the thread on is identical to the thread off.
- Gap 2: SDL3 Cocoa's swap does dispatch_sync to the main thread, which deadlocked while the main
  thread waited in a sync call. First filled by `SDL_MAC_OPENGL_ASYNC_DISPATCH=1` (found with `sample`),
  but then the context update ran on the main thread while the render thread drew: Metal aborted
  ("command encoder is already encoding", more often under load). Filled for good: no hint, and the
  game thread runs the main run loop while it waits for the render thread (`plat_render_waiting`,
  `CFRunLoopRunInMode`), so SDL's dispatch_sync is served. Regress under load: identical.
- New gap, refresh timing: refreshes timed from the swap gave 68 frames/s at 144 Hz. Filled by a
  display-grid loop on the render thread: batches run as they arrive, and at each refresh that has
  no new step frame it draws an in-between frame (78-80 frames/s).
- New gap, refresh mid-step: a refresh replayed after a partial batch (handed over for a readback)
  changed the drawing the game reads, so hashes differed from frame 12780. Filled: no refresh while
  a step is open (only after the step's swap), and only back-buffer draws are blended. Render
  targets the game reads (the shadow map) get the step's own drawing.
- New gap, camera on its way: shader draws were not blended, and the eye came from a static HUD
  view. Filled: vertex shader constants are blended (`consts_mix`: a register that jumps is a cut,
  so the step's own constants are drawn), and the eye comes from the first draw that moved.
- Still open: gap 3 (browser), gap 4 (allocation per call; not measured as a cost yet).

## Gap 3, the browser: findings and the design (2026-10-21)
- A worker that owns a transferred canvas shows a frame only when it returns to its event loop: a busy
  loop with emscripten_webgl_commit_frame never refreshes (emscripten issues #17816, #8307; settings
  reference: OFFSCREENCANVAS_SUPPORT, OFFSCREEN_FRAMEBUFFER). So the native `for (;;)` loop cannot be
  copied: the web render thread is a pthread with the canvas transferred
  (emscripten_pthread_attr_settransferredcanvases), its loop driven by the worker's
  requestAnimationFrame (emscripten_set_main_loop in the thread). Each callback runs the batches that
  came, then at most one in-between frame (interp_refresh), and returns: the browser shows it.
- Cost: while the worker is between callbacks, a game wait for the drawing (rq_sync) waits up to one
  refresh. Measured natively (perf line "queue waits"): 311 waits in ~12000 steps, all in loading
  (up to 27 a second), 0 in a match. So matches pay nothing; loading pays up to ~0.4 s a second.
- Open: resizes (the canvas element cannot be resized after transferControlToOffscreen: size the
  OffscreenCanvas in the worker), SDL2's emscripten window keeping its events on the main thread,
  the fallback without OffscreenCanvas (no render thread: today's serial path), and Safari's
  OffscreenCanvas WebGL2 in workers (16.4+).

## Gap 3 filled (2026-10-21; platform/web/render_thread.c)
- Chrome makes a worker's WebGL context only while the page thread is free (a busy page thread hung
  the first CreateDevice; repro: a worker getContext while main spins never returns). Once made, the
  worker draws and presents while the page thread is busy. So the worker makes the context as it
  starts (`d3d9w_context_create`), and the page thread yields (`plat_frame_yield`) until it is made.
- The worker's own loop: `emscripten_request_animation_frame_loop` (one tick a refresh: the steps'
  batches that came, the open step to its end, else one in-between frame), or for RT_TEST_HZ a timer
  grid with the last ms waited out (worker timers come up to a ms late: 114 ticks/s asked 144).
- The display's Hz is measured on the page thread (a worker has no `document`) and shared atomically.
- `det_period_ms` returns the time since its last call: read it once a frame (twice drew none of 600).
- Measured, solo match: 0 of 600 steps not drawn, the drawing ~1 ms a frame; at RT_TEST_HZ=144 about
  132 frames a second shown (88 before). Without OffscreenCanvas, or RT_RENDER_THREAD=0: the page
  thread draws, as before.
- Still open: Safari (16.4+ has worker WebGL2 in OffscreenCanvas; untested here), the shell's
  backdrop snapshot (toDataURL cannot read a transferred canvas; caught), gap 4.
