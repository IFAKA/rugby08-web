# render/webgl/ — the renderer: a D3D9 subset on WebGL2 (web) and GL 4.1 core (native)

Grown from LostMyCode/d3d9-webgl (MIT, `LICENSE`); our code now. The D3D8 layer (`runtime/d3d8`) is
its only client. Public API: `d3d9.h` (types + interfaces, grep it) plus the `d3d9w_*` extensions.

- `device.h` the device class, method → file map · `device.cpp` lifetime, Clear, Reset · `device_targets.cpp`
  render targets, viewport · `device_state.cpp` render/stage/sampler states, transforms, lights · `device_resources.cpp`.
- `draw.cpp` the four draw calls · `draw_setup.cpp` what every draw shares (uniforms, FVF attributes, stage textures).
- `program_ff.cpp` the fixed-function program (`glsl/ffp.vert`, `glsl/ffp.frag`, embedded at configure time)
  · `program_vs.cpp` / `program_ps.cpp` D3D8 shaders (MojoShader GLSL) · `locations.cpp` attribute/uniform table.
- `texture.cpp` `surface.cpp` `buffers.cpp` `sampler.cpp` `lighting.cpp` `trace.cpp` (draw trace) `factory.cpp` (IDirect3D9).
- `internal.h` shared state and helpers.

Verification: `tools/check/regress.sh` (deterministic runs: every pixel and state hash must match).
