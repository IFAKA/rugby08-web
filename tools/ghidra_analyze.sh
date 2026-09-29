#!/usr/bin/env bash
# Ghidra headless auto-analysis of the exe (project kept in ghidra/ for later post-scripts),
# then pcrecomp's DumpBounds.java -> analysis/ghidra_funcs.csv ("start,end" hex per function).
# Prints one summary line; full output in logs/ghidra.log.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 1
mkdir -p ghidra analysis logs
export JAVA_HOME=/opt/homebrew/opt/openjdk@21
AH=/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless
if [ -d ghidra/rugby08.rep ]; then
  src=(-process Rugby08_unpacked.exe -noanalysis)   # reuse the analyzed project
else
  src=(-import Rugby08_unpacked.exe -analysisTimeoutPerFile 14400)
fi
"$AH" ghidra rugby08 "${src[@]}" -scriptPath third_party/pcrecomp/tools/ghidra \
  -postScript DumpBounds.java "$PWD/analysis/ghidra_funcs.csv" > logs/ghidra.log 2>&1
rc=$?
echo "ghidra_analyze: exit=$rc $(grep -m1 'wrote bounds' logs/ghidra.log | sed 's/.*wrote/wrote/' | cut -c1-120)"
exit $rc
