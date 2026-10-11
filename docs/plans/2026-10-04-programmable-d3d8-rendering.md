# Programmable D3D8 Rendering Implementation Plan

> **For Codex:** Execute this plan task-by-task in the current session.

**Goal:** Make D3D8 programmable vertex shaders affect native/WebGL rendering instead of silently falling back to stale FVF state.

**Architecture:** Preserve the existing MojoShader translation, add active programmable-shader state to the D3D8 device bridge, and expose a runtime-owned programmable path in the d3d9-webgl backend. Keep fixed-function rendering for FVF draws. Correct declaration-derived texture-coordinate sizes in all FVF paths.

**Tech Stack:** C++, D3D8 compatibility bridge, d3d9-webgl, MojoShader, CMake/native runtime checks.

---

### Task 1: Add active shader state and declaration metadata

**Files:**
- Modify: `runtime/d3d8/d3d8_int.hpp`
- Modify: `runtime/d3d8/d3d8_device.cpp`

Record the active `Vs8`, expose its translated GLSL source/metadata to the host device, and retain declaration element sizes/offsets for programmable draws.

### Task 2: Implement programmable vertex-shader binding

**Files:**
- Modify: `third_party/d3d9-webgl/d3d9.h`
- Modify: `third_party/d3d9-webgl/d3d9.cpp`
- Modify: `runtime/d3d8/d3d8_device.cpp`

Add a host-side programmable vertex path that compiles/binds translated GLSL, uploads D3D8 constants, binds declaration attributes, and uses it for draws selected by `SetVertexShader`.

### Task 3: Correct FVF texture-coordinate layout handling

**Files:**
- Modify: `third_party/d3d9-webgl/d3d9.cpp`

Decode the FVF texture-coordinate size bits and use the actual component count/offset instead of assuming every coordinate is two floats.

### Task 4: Handle shader translation failures explicitly

**Files:**
- Modify: `runtime/d3d8/d3d8_vshader.cpp`
- Modify: `runtime/d3d8/d3d8_int.hpp`

Preserve failed-shader status and prevent an invalid translated program from being selected. Add diagnostics identifying the shader id and reason.

### Task 5: Build and runtime verification

Run focused native graphics checks, inspect the diff, and run a short interactive/native play session. Confirm the `SetVertexShader(programmable)` warning disappears and capture any remaining shader or runtime faults separately.
