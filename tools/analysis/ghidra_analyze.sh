#!/usr/bin/env bash
# Ghidra headless auto-analysis of the DE-OBFUSCATED exe (tools/lift/patch_exe.py: the protection's
# push/push/ret calls made real, else Ghidra ends functions at the fake ret), project kept in
# ghidra/ for later post-scripts, then pcrecomp's DumpBounds.java -> analysis/ghidra_funcs.csv.
# Prints one summary line; full output in logs/ghidra.log.
set -uo pipefail
cd "$(dirname "$0")/../.." || exit 1
mkdir -p ghidra analysis logs
export JAVA_HOME=/opt/homebrew/opt/openjdk@21
AH=/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless
.venv/bin/python tools/lift/patch_exe.py >/dev/null || exit 1
if [ -d ghidra/rugby08_deobf.rep ]; then
  src=(-process Rugby08_deobf.exe -noanalysis) # reuse the analyzed project
else
  src=(-import build/deobf/Rugby08_deobf.exe -analysisTimeoutPerFile 14400)
fi
"$AH" ghidra rugby08_deobf "${src[@]}" -scriptPath third_party/pcrecomp/tools/ghidra \
  -postScript DumpBounds.java "$PWD/analysis/ghidra_funcs.csv" >logs/ghidra.log 2>&1
rc=$?
echo "ghidra_analyze: exit=$rc $(grep -m1 'wrote bounds' logs/ghidra.log | sed 's/.*wrote/wrote/' | cut -c1-120)"
exit $rc
