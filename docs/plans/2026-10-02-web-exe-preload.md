# Web Executable Preload Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the browser build place `Rugby08_unpacked.exe` in Emscripten’s virtual filesystem before C `main()` runs.

**Architecture:** Use Emscripten’s built-in `--preload-file` support at link time, mapping the repository executable to `/Rugby08_unpacked.exe`. This preserves the existing runtime loader and keeps the large chunked `Game Files` asset system unchanged.

**Tech Stack:** CMake, Emscripten 6.0.10, WebAssembly virtual filesystem.

---

### Task 1: Add the executable preload

**Files:**
- Modify: `CMakeLists.txt` in the `EMSCRIPTEN` link options.

**Step 1:** Add `--preload-file Rugby08_unpacked.exe@/Rugby08_unpacked.exe` to the web link options.

**Step 2:** Reconfigure and build the web target.

**Step 3:** Verify the generated `.data` asset exists beside the JavaScript and Wasm outputs.

### Task 2: Verify startup behavior

**Step 1:** Serve the rebuilt `build/web` output.

**Step 2:** Load the local page and inspect runtime output.

**Step 3:** Confirm `cannot open Rugby08_unpacked.exe` is absent; record any subsequent game/runtime failure separately.
