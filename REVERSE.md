# REVERSE — facts about Rugby08_unpacked.exe

Facts only, each backed by a tool output in `analysis/` (gitignored). Regenerate them; don't trust memory.

## PE (pcrecomp `pe/pe_analyze.py`, `pe/extract_imports.py`)
- PE32 i386, MSVC linker 7.10, image base 0x400000, entry 0x87555C. The header timestamp is 2015-10-24, which suggests the unpacker rewrote it.
- Sections, each with raw size equal to virtual size (a flat unpacked dump):
  - `.text`: VA 0x1000, 0x4CB000 bytes (about 4.8 MB of code).
  - `.data`: VA 0x4CC000, 0xB97000 bytes. The IAT lives here, at 0x4CC07C and later.
  - `.idata`: VA 0x1063000, 0x2000 bytes.
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
