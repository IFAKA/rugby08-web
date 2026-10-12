#!/usr/bin/env python3
"""on.match_puppet evaluation (tools/net/match_puppet_check.sh): DIR holds host/friend/control runs with
"ms: pos" lines every frame and screenshots DIR/NAME/t_T.png at the match times T. Prints one summary
line; exit 0 = the friend plays the host's match."""

import os
import sys

from PIL import Image, ImageChops, ImageStat

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from match_compare import parse  # noqa: E402

TOL_CM = 1
MIN_FRAMES = 3000


def mean_diff(a, b):
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    return sum(ImageStat.Stat(ImageChops.difference(ia, ib)).mean) / 3


def main(d, times):
    host, friend = parse(f"{d}/host.out"), parse(f"{d}/friend.out")
    if not host or not friend:
        print(f"match_puppet_check: FAIL (no ms: pos lines from the {'host' if not host else 'friend'})")
        return 1
    rec = [f for f in friend if f["pup"] and f["t"] > 0]
    bad = [f for f in rec if f["pup"][0] > TOL_CM or f["pup"][3] > TOL_CM]
    if len(rec) < MIN_FRAMES:
        print(f"match_puppet_check: FAIL (only {len(rec)} friend frames compared with the host's, {MIN_FRAMES} needed)")
        return 1
    if bad:
        b = bad[0]
        print(
            f"match_puppet_check: FAIL ({len(bad)}/{len(rec)} frames off; first at t {b['t']:.2f}: players "
            f"{b['pup'][0]} cm, ball {b['pup'][3]} cm)"
        )
        return 1
    last_t = host[-1]["t"]
    fend = [f for f in friend if f["t"] <= last_t]
    if not fend or fend[-1]["score"] != host[-1]["score"]:
        got = fend[-1]["score"] if fend else None
        print(f"match_puppet_check: FAIL (score: host {host[-1]['score']} at t {last_t:.1f}, friend {got})")
        return 1
    shots, worse = [], []
    for t in times.split(","):
        names = [f"{d}/{n}/t_{t}.png" for n in ("host", "friend", "control")]
        if not all(os.path.exists(n) for n in names):
            continue
        same, other = mean_diff(names[1], names[0]), mean_diff(names[2], names[0])
        shots.append((t, same, other))
        if not same < other:
            worse.append(f"t {t}: {same:.1f} vs control {other:.1f}")
    # every picture closer to the host's than the control's, and on average within a third of it (the
    # HUD of each player's own input, set-piece selectors and power bars, differs by design)
    if shots and sum(s for _, s, _ in shots) >= sum(o for _, _, o in shots) / 3:
        worse.append("on average not within a third of the control's difference")
    if len(shots) < 5 or worse:
        print(f"match_puppet_check: FAIL (pictures: {len(shots)} compared; {'; '.join(worse[:3]) or 'too few'})")
        return 1
    avg = sum(s for _, s, _ in shots) / len(shots), sum(o for _, _, o in shots) / len(shots)
    print(
        f"match_puppet_check: ok ({len(rec)} frames within {TOL_CM} cm of the host's (players and ball), score "
        f"{host[-1]['score'][0]}-{host[-1]['score'][1]} as the host's, picture diff {avg[0]:.1f} vs {avg[1]:.1f} "
        f"for a friend playing its own match, {len(shots)} times)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
