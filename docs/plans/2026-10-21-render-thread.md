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
