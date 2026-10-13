#!/usr/bin/env python3
"""on.match_flow evaluation (tools/net/match_flow_check.sh): DIR holds host.out and friend.out of a
host-authoritative party that played two matches. Prints one summary line; exit 0 = the flow works."""

import re
import sys

LAUNCH = re.compile(r"ms: launch setup (\w+) \(own \w+\) teams (\d+) v (\d+) stadium (\d+) frame (\d+)")
FLOW = re.compile(r"ms: flow frame (\d+): (.*)")
STATE = re.compile(r"ms: flow frame (\d+): game state (\w+)")
POS = re.compile(r"ms: pos f (\d+) .* pup dt \S+ worst (\d+) cm heading \S+ blocks \d+/30 ball (\d+) cm")
CLOCK = re.compile(r"ms: f (\d+) phase \S+ clock ([\d.]+)")
QUANTUM_CM = 3
FRONTEND = "008CD620"


def read(path):
    return open(path, errors="replace").read().splitlines()


def flows(lines, what):
    return [int(m.group(1)) for m in map(FLOW.search, lines) if m and m.group(2).startswith(what)]


def fail(msg):
    print(f"match_flow_check: FAIL ({msg})")
    return 1


def main(d):
    hl, fl = read(f"{d}/host.out"), read(f"{d}/friend.out")
    hla = [m for m in map(LAUNCH.search, hl) if m]
    fla = [m for m in map(LAUNCH.search, fl) if m]
    if len(hla) < 2:
        return fail(f"the host launched {len(hla)} matches, 2 needed")
    setups = {m.group(1) for m in hla}
    if len(fla) < 2 or any(m.group(1) not in setups for m in fla[:2]):
        return fail(f"friend launches {[m.group(1) for m in fla]}, host's {sorted(setups)}")
    joins = flows(fl, "joining the host's match")
    # the host frames shown, per friend match
    starts = [int(m.group(5)) for m in fla[:2]] + [10**9]
    shown = [[], []]
    for m in map(POS.search, fl):
        if m:
            f = int(m.group(1))
            for k in range(2):
                if starts[k] <= f < starts[k + 1]:
                    shown[k].append((int(m.group(2)), int(m.group(3))))
    for k in range(2):
        if len(shown[k]) < 50:
            return fail(f"match {k + 1}: the friend showed {len(shown[k])} host frames (sampled), 50 needed")
        off = [s for s in shown[k] if s[0] > QUANTUM_CM or s[1] > QUANTUM_CM]
        if off:
            return fail(f"match {k + 1}: {len(off)} frames off the host's by more than {QUANTUM_CM} cm")
    # the ends: the friend back in its menus within 120 frames of leaving
    overs, leaves = flows(hl, "match over"), flows(fl, "the host's match is over")
    if len(overs) < 2 or len(leaves) < 2:
        return fail(f"match ends: host {len(overs)}, friend left {len(leaves)}")
    fe = [int(m.group(1)) for m in map(STATE.search, fl) if m and m.group(2).upper() == FRONTEND]
    for k, f in enumerate(leaves[:2]):
        back = [x for x in fe if x >= f]
        if not back or back[0] - f > 120:
            return fail(f"end {k + 1}: the friend was not back in its menus within 120 frames of frame {f}")
    # the pause: asked by the friend, the host's clock stopped until the friend's own RESUME reached the host
    # (its 'Y': each player's pause menu is its own; the host ignores friends' pads while paused)
    asked, paused, resumed = flows(fl, "pausing the host"), flows(hl, "paused"), flows(hl, "resumed")
    if not asked or not paused or not resumed:
        return fail(f"pause: friend asked {len(asked)}, host paused {len(paused)}, resumed {len(resumed)}")
    p0, p1 = paused[0], next((r for r in resumed if r > paused[0]), None)
    if p1 is None or p1 - p0 < 60:
        return fail(f"the host's pause lasted {None if p1 is None else p1 - p0} frames, 60 needed")
    clock = {int(m.group(1)): float(m.group(2)) for m in map(CLOCK.search, hl) if m}
    c0, c1 = clock.get(p0 + 5), clock.get(p1 - 5)
    if c0 is None or c0 != c1:
        return fail(f"the host's clock moved during the pause ({c0} -> {c1})")
    asks = flows(hl, "a friend resumes")
    cross = next((c for c in asks if p0 < c <= p1), None)
    if cross is None or p1 - cross > 10:
        return fail(f"the host resumed at {p1}, not on the friend's RESUME ({cross})")
    print(
        f"match_flow_check: ok (2 matches: the friend joined from its menus at {joins[:2]} with the host's setup, "
        f"showed {len(shown[0])}+{len(shown[1])} sampled host frames within {QUANTUM_CM} cm, left both within "
        f"120 frames of the host's end; its pause held the host's clock at {c0} for {p1 - p0} frames and its "
        f"RESUME resumed it {p1 - cross} frames after arriving)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
