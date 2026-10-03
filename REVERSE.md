# REVERSE — facts about Rugby08_unpacked.exe

Facts only, each backed by a tool output in `analysis/` (gitignored). Regenerate them; don't trust memory.

## PE (pcrecomp `pe/pe_analyze.py`, `pe/extract_imports.py`)
- PE32 i386, MSVC linker 7.10, image base 0x400000, entry 0x87555C. The header timestamp is 2015-10-24, which suggests the unpacker rewrote it.
- Sections as RVAs (add 0x400000 for the VA), each with raw size equal to virtual size (a flat unpacked dump):
  - `.text`: RVA 0x1000, 0x4CB000 bytes (about 4.8 MB of code). VA 0x401000..0x8CC000.
  - `.data`: RVA 0x4CC000, 0xB97000 bytes. The IAT lives here, at VA 0x8CC07C and later.
  - `.idata`: RVA 0x1063000, 0x2000 bytes. The image spans VA 0x400000..0x1465000.
- Imports: 226 across 9 DLLs.

| DLL | Count |
|-----|------:|
| KERNEL32 | 136 |
| USER32 | 56 |
| GDI32 | 17 |
| WINMM | 7 |
| ADVAPI32 | 5 |
| DSOUND | 2 |
| D3D8 | 1 |
| DDRAW | 1 |
| DINPUT | 1 |

- Entry points into COM, where the rest of the rendering, sound and input API is reached through vtables:
  - D3D8: `Direct3DCreate8`.
  - DDRAW: `DirectDrawCreate`, probably only for the video-memory query.
  - DINPUT: `DirectInputCreateA`.
  - DSOUND: ordinal 1 (`DirectSoundCreate`) and ordinal 6 (`DirectSoundCaptureCreate`).
- WINMM: `timeGetTime`, `timeBeginPeriod`, `timeEndPeriod`, `timeSetEvent`, `timeKillEvent`, `waveOutGetNumDevs`, `timeGetDevCaps`.
- ADVAPI32: registry reads only (`RegOpenKey[Ex]A`, `RegQueryValueExA`, `RegEnumValueA`, `RegCloseKey`).
- Full list: `analysis/imports.json`. Raw tool output: `logs/extract_imports.log`.

## Function recovery (pcrecomp disasm32 vs Ghidra 12.1 headless; `tools/score.sh`, `tools/build_catalog.py`)
- disasm32: 54,031 functions, 92.5% byte coverage. Ghidra: 26,664 functions in `.text`, 88.5% byte coverage.
- Scored against Ghidra: recall: 0.9941, precision 0.558. False positives are 7,721 splits (inside a Ghidra function) and 13,281 invented (outside every one).
- The "invented" starts are real code that Ghidra skips because only data reaches them:
  - MSVC C++ EH unwind funclets (`lea ecx,[ebp-X]; jmp dtor`).
  - EH handler stubs (`mov eax, FuncInfo; jmp 0x8715E3` = __CxxFrameHandler).
  - `_initterm` static initializers (`fld; fstp; ret`).
  - Most sit in 0x800000..0x8CC000, and the 85 KB block at 0x8939D3..0x8A9120 is almost all unwind funclets.
- Lift catalog (`analysis/lift_catalog.json`), 37,882 entries:
  - All 26,664 Ghidra starts, plus 123 RTTI methods Ghidra lacked.
  - 11,094 invented starts that decode cleanly to ret/jmp. 2,756 invented were rejected as junk.
  - 1 extra direct-call target from the closure.
  - The 7,032 split starts are excluded on purpose, because an entry mid-function would cut it apart.
- RTTI (`tools/rtti.sh`): 704 classes with vtables (matches `rugby08_classes_and_sources.txt`) and 2,211 virtual-method entries.
- Instruction census (`tools/insn_stats.py`): 2.2M instructions in Ghidra bodies. 88k x87, 5.4k MMX (94 funcs), 3.1k SSE (65 funcs, CPU-dispatched CRT math), and `fs:` in 2,151 funcs.

## Entry points (from disassembly of the CRT entry 0x87555C)
- WinMainCRTStartup is 0x87555C. It calls GetModuleHandleA(0) and does the MZ/PE check, then heap and IO init, GetCommandLineA and GetStartupInfoA. Finally it calls `WinMain(hInstance, 0, lpCmdLine, nShowCmd)` at **0x812FC0**.
- Traced function hooks (`tools/hooks.json`) make a lifted body log `<name> entered` on entry.

## Protection-layer code transforms (SecuROM 7.33 per release NFO; `tools/code_patch.py`, `tools/ranges.py`)
- Obfuscated calls, rewritten for the lifter only (guest image untouched) as `call T; jmp R`:
  `push R; push T; ret` (3,937 sites) and `push R; jmp T` (3,938 near + 72 short), R = 4/8/12/16 bytes past the site, junk in between.
  Lifting `push R; jmp T` literally made T's `ret` leave the lifted caller, so R's epilog never ran (esp/ebp off by 4-12 at boot).
- Stolen prologues: `jmp [ptr]` stubs in .text (e.g. 0x421550 -> [0x1384B08] = 0x45C020) whose target is a whole function, so they're tail jumps.
- Trigger counters: `dec dword [X]; je <check>` throughout game code (e.g. 0x45C027). Lifted as written.
