#!/bin/sh
# web.split_check: the web build's split functions (tools/lift/split_big.py) must behave like the unsplit ones.
# Builds build/native-split (native, with build/web-gen) and replays main menu -> MY RUGBY -> Squad
# Management in det mode, unpaced. Before the jump-table fix the split sub_005A55B0 sent every computed
# jump to its first exit stub: MY RUGBY did not open and the next press crashed (2026-10-12).
# Pass: the run reaches its last frame with no fault and no unresolved jump. One summary line;
# logs/split_check{,_run,_out}.log.
cd "$(dirname "$0")/../.." || exit 1
python3 -I tools/lift/split_big.py gen/recomp build/web-gen >logs/split_check.log 2>&1 || {
  echo "split_check: FAIL (split_big.py)"
  exit 1
}
cmake -S . -B build/native-split -DCMAKE_BUILD_TYPE=RelWithDebInfo -DRECOMP_SPLIT=ON >>logs/split_check.log 2>&1 &&
  cmake --build build/native-split -j >>logs/split_check.log 2>&1 || {
  echo "split_check: FAIL (build, logs/split_check.log)"
  exit 1
}
RT_DET=1 RT_FPS=0 SDL_AUDIODRIVER=dummy timeout 900 build/native-split/rugby08 --frames 10400 --log logs/split_check_run.log \
  --replay tests/replays/my_rugby.txt >logs/split_check_out.log 2>&1
RC=$?
bad=$(grep -m1 -E 'FAULT|unresolved' logs/split_check_run.log)
if [ $RC -eq 0 ] && [ -z "$bad" ] && grep -q 'frame limit 10400' logs/split_check_out.log; then
  echo "split_check: ok (split build opens MY RUGBY and Squad Management, no fault)"
  exit 0
fi
echo "split_check: FAIL (exit $RC; ${bad:-no last frame} )"
exit 1
