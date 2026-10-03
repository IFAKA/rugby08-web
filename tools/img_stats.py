#!/usr/bin/env python3
"""Stats of a screenshot (rugby08 --screenshot). One summary line.
  tools/img_stats.py FILE.png [--not-blank] [--min-colors N]
--not-blank: exit 1 unless the image has at least 2 distinct colours and some pixel brighter than
             near-black (a cleared frame is one colour).
--min-colors N: exit 1 unless it has at least N distinct colours (menus, not text-on-black)."""
import sys

args = sys.argv[1:]
if not args:
    print("img_stats: FAIL (no file)"); sys.exit(2)
path = args[0]
need_colors = int(args[args.index("--min-colors") + 1]) if "--min-colors" in args else 0
not_blank = "--not-blank" in args
try:
    from PIL import Image
    im = Image.open(path).convert("RGB")
except Exception as e:  # noqa: BLE001 -- any decode failure is a failed check
    print(f"img_stats: FAIL ({path}: {e})"); sys.exit(1)
w, h = im.size
colors = im.getcolors(maxcolors=1 << 24) or []
ncol = len(colors)
bright = max((max(c) for _, c in colors if isinstance(c, tuple)), default=0)
ok = (not not_blank or (ncol >= 2 and bright > 16)) and ncol >= need_colors
print(f"img_stats: {'ok' if ok else 'FAIL'} ({w}x{h}, {ncol} colours, brightest channel {bright})")
sys.exit(0 if ok else 1)
