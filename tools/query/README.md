# tools/query/ — look things up in generated code without reading it whole

`fn.sh ADDRESS|NAME|fragment [--max N]` prints one lifted function from `gen/recomp` (with its readable
name from `analysis/symbols.json`, built by `tools/analysis/symbols.py` from the RTTI vtables).

`callgraph.py callees|callers VA|NAME [--depth N]`, `path A B`, `reach A B`: the call graph read from the
lifted C (direct calls; protector-slot tails resolved from the exe; virtual calls listed by slot, not
followed), cached in `analysis/callgraph.json`. Studies execution paths without running the game.
