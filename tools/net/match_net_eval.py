#!/usr/bin/env python3
"""on.match_net evaluation (tools/net/match_net_check.sh): DIR holds host.out and friend.out of a
host-authoritative session. Prints one summary line; exit 0 = the friend plays the host's match over the
network and its pad plays on the host."""

import os
import re
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from match_compare import parse  # noqa: E402

QUANTUM_CM = 3  # the replay coder: positions in whole 1/52.5 m units (1.9 cm per axis)
MIN_FRAMES = 3000
MAX_LAG = 6
FRAME_NS = 1e9 / 60
LAUNCH = re.compile(r"ms: launch setup \w+ \(own \w+\) teams (\d+) v (\d+) stadium (\d+)")
NET = re.compile(r"ms: net (tx|rx) (\d+) \w+ ns (\d+)")
PAD = re.compile(r"ms: net pad 1 frame (\d+) buttons \w+ stick (-?\d+),(-?\d+)")
CTL = re.compile(r"ms: f (\d+) .* ctl (-?\d+)/(-?\d+) ")


def lines(path):
    return open(path, errors="replace").read().splitlines()


def fail(msg):
    print(f"match_net_check: FAIL ({msg})")
    return 1


def pad_effect(host_lines, host_pos):
    """The friend's stick press in the match: how far its controlled player went before and during it."""
    launch = next(
        (
            int(m.group(1))
            for m in map(re.compile(r"frame (\d+)$").search, (x for x in host_lines if "ms: launch" in x))
            if m
        ),
        0,
    )
    presses, start = [], None
    for x in host_lines:
        m = PAD.search(x)
        if not m or int(m.group(1)) < launch:
            continue
        if int(m.group(2)) > 0 and start is None:
            start = int(m.group(1))
        elif int(m.group(2)) <= 0 and start is not None:
            presses.append((start, int(m.group(1))))
            start = None
    if not presses:
        return None
    s, e = presses[0]
    ctl = {}
    for x in host_lines:
        m = CTL.search(x)
        if m:
            ctl[int(m.group(1))] = int(m.group(3))
    by_f = {p["f"]: p for p in host_pos}
    c = ctl.get(s, -1)
    if c < 0 or any(f not in by_f for f in (s - 60, s, e)):
        return (s, e, c, None, None)

    def at(f):
        return by_f[f]["p"][15 + c]

    def dist(a, b):
        return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5 / 100

    return (s, e, c, dist(at(s - 60), at(s)), dist(at(s), at(e)))


def main(d):
    hl, fl = lines(f"{d}/host.out"), lines(f"{d}/friend.out")
    hlaunch = next((LAUNCH.search(x) for x in hl if LAUNCH.search(x)), None)
    flaunch = next((LAUNCH.search(x) for x in fl if LAUNCH.search(x)), None)
    if not hlaunch or not flaunch:
        return fail(f"no match launch on the {'host' if not hlaunch else 'friend'}")
    if hlaunch.groups() != flaunch.groups():
        return fail(
            f"friend played teams {flaunch.group(1)} v {flaunch.group(2)}, host {hlaunch.group(1)} v {hlaunch.group(2)}"
        )
    host, friend = parse(f"{d}/host.out"), parse(f"{d}/friend.out")
    rec = [f for f in friend if f["pup"] and f["t"] > 0]
    bad = [f for f in rec if f["pup"][0] > QUANTUM_CM or f["pup"][3] > QUANTUM_CM]
    if len(rec) < MIN_FRAMES:
        return fail(f"only {len(rec)} friend frames compared with the host frame shown, {MIN_FRAMES} needed")
    if bad:
        return fail(
            f"{len(bad)}/{len(rec)} frames off; first at t {bad[0]['t']:.2f}: {bad[0]['pup'][0]} cm, ball {bad[0]['pup'][3]} cm"
        )
    tx, lags = {}, []
    for x in hl:
        m = NET.search(x)
        if m and m.group(1) == "tx":
            tx[int(m.group(2))] = int(m.group(3))
    for x in fl:
        m = NET.search(x)
        if m and m.group(1) == "rx" and int(m.group(2)) in tx:
            lags.append((int(m.group(3)) - tx[int(m.group(2))]) / FRAME_NS)
    if not lags:
        return fail("no frame went from the host to the friend")
    p95 = sorted(lags)[int(0.95 * (len(lags) - 1))]
    if p95 > MAX_LAG:
        return fail(f"latency p95 {p95:.1f} frames (median {statistics.median(lags):.1f}), {MAX_LAG} allowed")
    last_t = min(friend[-1]["t"], host[-1]["t"])
    hs = [h for h in host if h["t"] <= last_t][-1]["score"]
    fs = [f for f in friend if f["t"] <= last_t][-1]["score"]
    if hs != fs:
        return fail(f"score at t {last_t:.1f}: host {hs}, friend {fs}")
    pe = pad_effect(hl, host)
    if not pe or pe[3] is None:
        return fail(f"the friend's pad press did not reach a controlled away player on the host ({pe})")
    s, e, c, before, during = pe
    if not (before < 1.5 and during > 5):
        return fail(
            f"friend's player {c} moved {before:.1f} m in the second before its press, {during:.1f} m during it (host frames {s}-{e})"
        )
    print(
        f"match_net_check: ok ({len(rec)} friend frames = the host frame shown within {QUANTUM_CM} cm, {len(lags)} frames "
        f"sent, latency median {statistics.median(lags):.1f} p95 {p95:.1f} frames, score {fs[0]}-{fs[1]}; the friend's pad "
        f"moved away player {c} {during:.1f} m (still before: {before:.1f} m))"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
