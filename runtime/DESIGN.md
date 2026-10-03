# Runtime design (decisions; change them here first)

## ABI the lifted code expects (pcrecomp `runtime/recomp32/recomp_types.h`)
- Registers are globals (`g_eax`…`g_ebp`), and the x87 stack (`g_st`, `g_fp_top`, `g_fpu_cw`), lazy flags (`g_flag_*`) and `g_cur_func` are globals too.
  - The game uses threads, so all of these are `_Thread_local`. We vendor `recomp_types.h` into `runtime/include/recomp/` and add `RECOMP_TLS` to those declarations. Build the generated code with `RECOMP_LOCAL_REGS` so the TLS is touched only at call boundaries.
- Memory: `ADDR(va) = (uint32_t)va + g_mem_base`, with one guest arena:
  - Native (arm64 macOS): `mmap` 4 GiB reserved with `MAP_NORESERVE`, so every 32-bit VA is backed.
  - Web (wasm32): arena `malloc`ed, 1 GiB. Addition wraps mod 2^32.
  - Flat mode (VA == host pointer) is impossible natively, because arm64 macOS keeps a 4 GiB __PAGEZERO. Both targets use the offset for uniformity.
- Guest layout:
  - The whole PE image is copied to VA 0x400000..0x1465000. That includes `.text`, since MSVC puts switch tables and constants there.
  - Main-thread stack: 0x00100000..0x00300000 (2 MiB). Each other thread gets a 1 MiB stack from the guest heap.
  - Guest heap: dlmalloc mspace (from emscripten's `system/lib/dlmalloc.c`, CC0) over 0x02000000..(arena end − 16 MiB). It backs HeapAlloc, VirtualAlloc, GlobalAlloc and LocalAlloc.
  - Per-thread TIB (`g_fs_base`): the SEH chain at fs:[0], the stack bounds, and the TLS slots array at fs:[0x2C].
- Calls: `RECOMP_ICALL(va)` pushes a dummy return address, then looks up in order `recomp_lookup_manual`, the generated table, then `recomp_lookup_import`. We supply the manual and import lookups.

## Lifter choice (measured with `tools/insn_stats.py` → `analysis/insn_stats.json`)
- 2.2M instructions: 88k x87, 5.4k MMX (94 funcs), 3.1k SSE/SSE2 (65 funcs), and `fs:` in 2,151 funcs (SEH).
- Lifter: `lift32` via `python -m tools`, the global-register model with RECOMP_TLS. It has MMX and `fs:`, but no XMM.
  - `lift32_cpu` handles XMM, but is a separate driver and slower (a CPU struct behind a pointer).
- Modeled CPU: Pentium Pro (CPUID 0x619: FPU, TSC, CX8, CMOV/FCMOV; no MMX, SSE or 3DNow!). The MMX, SSE and 3DNow! code (math, D3DX-style kernels, codecs) is CPU-dispatched, so those paths never run.
- `tools/lift_ext.py` gives every instruction that CPU's semantics:
  - It implements what lift32 lacks (`fucompp`, `fcomi`, `fcmov`, `ficom`, BCD adjusts, `xadd`, `bound`, `arpl`, `lahf`…).
  - Extensions lift as #UD faults and privileged or I/O instructions as #GP faults. Both go to `recomp_fault` and are loud.
  - If a trace ever hits an XMM function, lift that one function with `lift32_cpu`, or add the ops to lift32.

## x87 model limits (by design, from pcrecomp)
- The x87 stack holds C doubles (53-bit). 80-bit `fld/fstp xword` rounds to double, and transcendentals use libm.
  - Both differ from hardware only in the last bits. The difftest skips such bodies, and everything else must match bit for bit.
- The x87's 15-bit exponent range is not modeled (the model overflows and underflows at double range), and neither are the status-word exception flags (bits 0–5).
- NaN payload and sign are not modeled. The x87 default NaN is negative and ARM's is positive, and wasm makes NaN bits nondeterministic, so the difftest's inputs contain no NaN or Inf patterns.

## Function bodies (tools/lift.py `body_ends`)
- A Ghidra function is lifted over its whole extent, capped at the next Ghidra start, decoding straight over any other entry inside it.
  - The old rule (stop at the next catalog entry) turned loops that crossed an inner entry into nested tail calls. That means unbounded host recursion.
- An entry inside a Ghidra function runs to that function's end. Other entries stop at the next top-level entry.
- Code shared by two entries is lifted twice, which is correct and costs only size.

## Compiling generated code
- Always use `-fwrapv -fno-strict-aliasing`. lift32 emits signed `imul` as `(int32_t)a * (int32_t)b`, and signed overflow is UB: clang then widened an address computation past 4 GiB, which the difftest caught in sub_00890DA0. MEM8/16/32/F/D pun one byte array through many types.
- Use `-DRECOMP_LOCAL_REGS`, which keeps registers in locals and touches the TLS globals only at call boundaries. The difftest compiles exactly like this.

## Win32/COM boundary ("bridges")
- The IAT holds 226 slots at VA 0x8CC07C..0x8CC39C (RVAs in `pe_analysis.json` `.imports[].iat_rva`). At boot, slot i ← `BRIDGE_IMPORT_BASE + 16*i`, where `BRIDGE_IMPORT_BASE` = 0xFFF00000. That range is never dereferenced, only dispatched.
- COM objects (IDirect3D8, IDirect3DDevice8…, IDirectSound…, IDirectInput…) live in guest memory:
  - Layout: `{ u32 vtbl_va; u32 host_index; }`.
  - Each vtable is a guest array of bridge VAs in the 0xFFE00000 range.
  - Bridges are `void f(void)`: they read args at `MEM32(esp+4+4k)`, set `eax`, and do `esp += 4 + 4*argc` (stdcall; `this` is arg 0 for COM).
  - Bridge tables are generated from signature lists (`runtime/gen_bridges.py`), not hand-written.
- Callbacks into guest code go through `guest_call(va, n, args…)` (WndProc, thread start, timeSetEvent, DirectInput/DirectSound enumerate callbacks):
  - Push args right to left, push RECOMP_RETADDR, then call `recomp_lookup(va)()`.
  - Save and restore callee-saved registers around the call.
- Unimplemented imports: an auto-generated stub logs `UNIMPL <dll>!<name>` once, then returns 0.

## Graphics / audio / input / files
- D3D8 → guest COM bridges (runtime/d3d8/) that translate each D3D8 call to D3D9 → d3d9-webgl.
  - Not d3d8to9 itself: it does not compile against d3d9-webgl's `d3d9.h`, which is a subset (2,242 errors on 2026-10-03: no cube/volume textures, swap chains, IUnknown/resource base methods, ~40 device methods). The bridge layer has to convert every call for the guest anyway (32-bit structs, guest lock buffers), so it does the D3D8→D3D9 step too, using d3d8to9's sources as the reference for the conversion rules.
  - Web: WebGL2 as-is.
  - Native: an SDL2 GL 3.3 core context, with the shader header swapped from `#version 300 es` to `#version 330 core`, patched in `platform/native/`.
  - The game is fixed-function only (the exe has no shader strings), so d3d8to9's shader-assembly path is stubbed.
- DirectSound → mixer on SDL audio. DirectInput → SDL keyboard and gamecontroller.
- Files: guest paths (backslashes, any case) map to `Game Files/` through a case-insensitive lookup cache.
  - Native: host FS.
  - Web: the emscripten FS, populated from lazily fetched chunked bundles (`build/web/data/`).
- Threads:
  - Native: pthreads.
  - Web: emscripten pthreads (needs COOP/COEP). The main loop runs on a pthread (PROXY_TO_PTHREAD) so the game can block.
