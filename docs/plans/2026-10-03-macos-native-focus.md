# macOS Native Focus Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Launch the native Rugby 08 build as a macOS application and prevent unfocused cursor warps from stealing the user's pointer.

**Architecture:** Keep the existing native executable and create a lightweight ignored `.app` bundle around it at launch time. The SDL platform layer will explicitly raise shown windows and only apply real cursor warps while the game window has input focus; automated virtual-cursor behavior remains unchanged.

**Tech Stack:** POSIX shell, macOS `open`, SDL2, CMake.

---

### Task 1: Package and launch the native build as an app

**Files:**
- Modify: `scripts/play.sh`

**Steps:**
1. Preserve the existing build-if-missing behavior.
2. Recreate `build/native/Rugby08.app/Contents/{MacOS,Resources}` and write a minimal `Info.plist`.
3. Copy `build/native/rugby08` as the bundle executable so the app bundle has an independent code-signing payload.
4. Launch with `open -n ... --args`, passing absolute paths for the game executable, content directory, and log.

### Task 2: Preserve normal SDL focus behavior

**Files:**
- Modify: `platform/sdl/sdl_plat.c`

**Steps:**
1. Keep showing and hiding behavior limited to `SDL_ShowWindow`/`SDL_HideWindow`.
2. Let the app bundle provide initial activation without raising the window on every Win32 visibility/position update.

### Task 3: Guard real cursor warps by focus

**Files:**
- Modify: `platform/sdl/sdl_plat.c`

**Steps:**
1. Leave virtual cursor updates untouched for frame-limited/replay runs.
2. Return without warping when the native SDL window is not focused.
3. Build the native target and run the focused native smoke check.
