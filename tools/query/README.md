# tools/query/ — look things up in generated code without reading it whole

`fn.sh ADDRESS|NAME|fragment [--max N]` prints one lifted function from `gen/recomp` (with its readable
name from `analysis/symbols.json`, built by `tools/analysis/symbols.py` from the RTTI vtables).
