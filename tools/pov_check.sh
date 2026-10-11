#!/bin/sh
# Per-player camera: the same match frame (play_now_match.txt, deterministic mode) drawn with the game
# camera and with preset N must differ (the local view is applied), with no other change to the run.
#   tools/pov_check.sh [--preset N]
cd "$(dirname "$0")/.." || exit 1
N=3
[ "$1" = --preset ] && N=$2
mkdir -p logs/pov
grep -v ' shot ' tests/replays/play_now_match.txt > logs/pov/replay.txt
for p in 0 $N; do
  sed '$a\
' logs/pov/replay.txt > logs/pov/r$p.txt; echo "14900 shot logs/pov/check$p.png" >> logs/pov/r$p.txt
  RT_DET=1 RT_POV=$p RT_FPS=0 SDL_AUDIODRIVER=dummy timeout -s KILL 600 build/native/rugby08 --frames 14950 \
    --replay logs/pov/r$p.txt --log logs/pov/check$p.log >/dev/null 2>&1 &
done
wait
python3 -I - "$N" <<'P'
import sys
from PIL import Image, ImageChops
n = sys.argv[1]
a = Image.open('logs/pov/check0.png').convert('RGB'); b = Image.open(f'logs/pov/check{n}.png').convert('RGB')
box = ImageChops.difference(a, b).getbbox()
area = 0 if not box else (box[2] - box[0]) * (box[3] - box[1])
ok = area > a.size[0] * a.size[1] // 4
print(f"pov_check: {'ok' if ok else 'FAIL'} (preset {n} changes {area} of {a.size[0]*a.size[1]} pixels' bounding area)")
sys.exit(0 if ok else 1)
P
