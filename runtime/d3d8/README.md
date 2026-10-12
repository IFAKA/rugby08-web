# runtime/d3d8/ — Direct3D 8 for the game, on the renderer's D3D9 API

Guest COM objects `{vtbl, host index}`; vtables are generated (`tools/gen_d3d8_bridges.py`) and bind
`HLE_D3D(Iface, Method)` handlers. Conversion rules follow `third_party/d3d8to9`.

- `d3d8_core.cpp` object model, IUnknown, IDirect3D8, CreateDevice, caps.
- `d3d8_device.cpp` device lifetime, Reset/Present, render targets, scene, draws · `d3d8_state.cpp` states,
  transforms, lights, streams · `d3d8_dev_shaders.cpp` shader handles · shared: `d3d8_dev.hpp`, `d3d8_int.hpp`.
- `d3d8_levels.cpp` texture formats and levels (guest shadows, upload, readback) · `d3d8_textures.cpp` texture
  and surface interfaces · `d3d8_buffers.cpp` vertex/index buffers · `d3d8_vshader.cpp` vs/ps translation (MojoShader).
