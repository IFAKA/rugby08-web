#!/usr/bin/env python3
"""on.cross_platform evaluation (tools/net/cross_platform_check.sh): DIR/a (native host native.out, browser
friend web.log) and DIR/b (browser host web.log, native friend native.out). Prints one summary line."""

import re
import sys

LAUNCH = re.compile(r"ms: launch setup (\w+) \(own \w+\) teams (\d+) v (\d+) stadium (\d+)")
PUP = re.compile(r"pup dt \S+ worst (\d+) cm heading \S+ blocks \d+/30 ball (\d+) cm")
QUANTUM_CM, MIN_SAMPLES = 3, 50


def read(path):
    try:
        return open(path, errors="replace").read()
    except OSError:
        return ""


def friend_ok(name, host, friend):
    h, f = LAUNCH.search(host), LAUNCH.search(friend)
    if not h:
        return f"{name}: the host never launched a match"
    if not f or f.group(1, 2, 3, 4) != h.group(1, 2, 3, 4):
        return f"{name}: the friend did not play the host's match ({f.groups() if f else None} vs {h.groups()})"
    samples = [tuple(map(int, m.groups())) for m in PUP.finditer(friend)]
    off = [s for s in samples if s[0] > QUANTUM_CM or s[1] > QUANTUM_CM]
    if len(samples) < MIN_SAMPLES or off:
        return (
            f"{name}: the friend showed {len(samples)} sampled host frames, {len(off)} off by more than {QUANTUM_CM} cm"
        )
    return None, len(samples)


def main(d):
    a = friend_ok("native host, browser friend", read(f"{d}/a/native.out"), read(f"{d}/a/web.log"))
    b = friend_ok("browser host, native friend", read(f"{d}/b/web.log"), read(f"{d}/b/native.out"))
    for r in (a, b):
        if isinstance(r, str):
            print(f"cross_platform_check: FAIL ({r})")
            return 1
    print(
        f"cross_platform_check: ok (native host -> browser friend: {a[1]} sampled frames, browser host -> native "
        f"friend: {b[1]}, each within {QUANTUM_CM} cm of the host's)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
