#!/usr/bin/env python3
"""Compare two tools/check/regress.sh captures. Last line is the summary; exit 0 = within tolerance.
  regress_cmp.py BASE DIR
Captures are deterministic (RT_DET=1), so images must be pixel-identical (--tolerant: mean absolute
difference <= 6 and <= 8% of pixels off by more than 48, for runs that are not). Audio: RMS within
3 dB. Exit codes, log signatures and guest state hashes must match exactly."""

import math
import pathlib
import sys
import wave

from PIL import Image, ImageChops

TOLERANT = "--tolerant" in sys.argv
MAX_MEAN = 6.0 if TOLERANT else 0.0  # mean abs diff per channel
MAX_FAR = 0.08 if TOLERANT else 0.0  # share of pixels with a channel off by > 48 (exact: any difference)


def img_diff(a, b):
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if ia.size != ib.size:
        return None, None
    d = ImageChops.difference(ia, ib)
    hist = d.histogram()
    n = ia.size[0] * ia.size[1]
    mean = sum(sum(i * c for i, c in enumerate(hist[k * 256 : (k + 1) * 256])) for k in range(3)) / (3 * n)
    far = sum(1 for p in d.get_flattened_data() if max(p) > (48 if TOLERANT else 0)) / n
    return mean, far


def rms_db(path):
    w = wave.open(str(path), "rb")
    raw = w.readframes(w.getnframes())
    s = memoryview(raw).cast("h")
    if not len(s):
        return -120.0
    acc = sum(x * x for x in s[:: max(1, len(s) // 400000)])
    n = len(s[:: max(1, len(s) // 400000)])
    return 20 * math.log10(max(1e-9, math.sqrt(acc / n) / 32768))


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    base, cur = pathlib.Path(args[0]), pathlib.Path(args[1])
    bad = []
    for img in sorted(base.rglob("*.png")):
        rel = img.relative_to(base)
        other = cur / rel
        if not other.exists():
            bad.append(f"{rel}: missing")
            continue
        mean, far = img_diff(img, other)
        if mean is None:
            bad.append(f"{rel}: size differs")
            continue
        ok = mean <= MAX_MEAN and far <= MAX_FAR
        print(f"{'ok  ' if ok else 'DIFF'} {rel}: mean {mean:.2f}, far {far:.3f}")
        if not ok:
            bad.append(f"{rel} (mean {mean:.1f}, far {far:.2f})")
    for wav in sorted(base.glob("*.wav")):
        other = cur / wav.name
        if not other.exists():
            bad.append(f"{wav.name}: missing")
            continue
        a, b = rms_db(wav), rms_db(other)
        print(f"{'ok  ' if abs(a - b) <= 3 else 'DIFF'} {wav.name}: {a:.1f} dB vs {b:.1f} dB")
        if abs(a - b) > 3:
            bad.append(f"{wav.name} ({a:.1f} vs {b:.1f} dB)")
    for f in ("status.txt", *sorted(p.name for p in [*base.glob("*.sig"), *base.glob("*.det")])):
        x, y = (base / f).read_text(), (cur / f).read_text() if (cur / f).exists() else ""
        if x != y:
            print(f"DIFF {f}:\n--- base\n{x}--- now\n{y}")
            bad.append(f)
    n = len(list(base.rglob("*.png"))) + len(list(base.glob("*.wav")))
    if bad:
        print(f"regress: FAIL ({len(bad)} of {n} differ: {', '.join(bad[:6])})")
        return 1
    print(
        f"regress: ok ({n} images/audio {'within tolerance' if TOLERANT else 'identical'}, exit codes, log signatures and state hashes identical)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
