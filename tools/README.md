# tools/ — scripts, grouped by job

Every script prints ONE summary line and writes details to `logs/<name>.log` (project rule).

| dir | what |
|---|---|
| `lift/` | exe → C: `lift.py` (+ `lift_ext.py`, `code_patch.py`, `jumptables.py`, `ranges.py`, `stubemu.py`), the catalog (`build_catalog.py`, `icall_seed.py`, `lift_fixpoint.sh`), sound-bank code (`aems_lift.py`), checks of the lift (`fidelity_report.py`, `difftest.py`, `lift_report.sh`), `split_big.py` (web), `patch_exe.py` (deobfuscated exe), `vendor_recomp_types.py` |
| `gen/` | generated runtime tables CMake runs: `gen_imports.py` (import bridges), `gen_d3d8_bridges.py` (D3D8 vtables) |
| `check/` | feature checks and gates: `run_native.sh`, `web_check.sh`, `serve_check.sh`, `run_wine.sh` (oracle), `det_synctest.sh`, `regress.sh` (refactor referee), `lint.sh`, `structure.sh`, image/audio/manifest stats |
| `web/` | `serve.py` (localhost, COOP/COEP), `package_assets.py` (Game Files → 8 MiB chunks), `stamp_sw.cmake` (the app's service worker per build), `web_selftest.py`, `web_profile.py` |
| (top) | `setup_submodules.sh`: a fresh clone's third_party/ (upstream submodules + our patch series) |
| `analysis/` | Ghidra, RTTI, disassembly, instruction stats, recovery score, Wine specs |
| `oracle/` | Wine proxy DLLs that trace the original exe's D3D8/DInput/DSound calls |
| `hooks.json` | function entries the lift instruments (`NAME entered`, probes) |
