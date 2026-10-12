# tools/gen/ — tables generated at build time (CMake runs these)

`gen_imports.py` → `gen/runtime/imports.c` (IAT slots → `HLE(Name)` handlers, stdcall pops from Wine specs);
`gen_d3d8_bridges.py` → `gen/runtime/d3d8_vtables.c` (D3D8 vtable order from d3d8to9 → `HLE_D3D` handlers).
