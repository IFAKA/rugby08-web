# tools/lift/ — the static recompiler pipeline (Rugby08_unpacked.exe → gen/)

`lift_fixpoint.sh` runs `build_catalog.py` + `lift.py` to a fixed point; `aems_lift.py` lifts the sound-bank
code. `lift.py` drives pcrecomp's lift32 with `lift_ext.py` (extra instructions), `code_patch.py` (protector
call sites), `jumptables.py`, `ranges.py` (code ranges), `stubemu.py` (protector stubs). Checks of the
output: `fidelity_report.py`, `difftest.py` (lifted C vs Unicorn), `lift_report.sh`. Never edit `gen/`.
