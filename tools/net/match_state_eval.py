#!/usr/bin/env python3
"""on.match_map evaluation of the "ms:" lines (runtime/core/match_state.c, RT_MS_LOG=60) of a native
deterministic run of tests/replays/match_play.txt. Prints one summary line; exit 0 = consistent.

Expected values come from the same run's screenshots (docs/MATCH_STATE.md): the HUD shows 0-3 from the
New Zealand penalty (frame ~17450) until the try at ~26800, 0-10 after the conversion; the HUD clock
equals the decoded clock; in open play the clock runs 88 game seconds per 600 frames (this match
length), i.e. 8.8 per logged line.
"""

import re
import sys

LINE = re.compile(
    r"ms: f (\d+) phase (\S+) clock (\S+) score (\d+)-(\d+) ball \S+ holder (-?\d+)/(-?\d+) hd (\S+) "
    r"in (\d+) parked (\d+) out (\d+) ctl (-?\d+)/(-?\d+)"
)


def main(path):
    rows = []
    for line in open(path, errors="replace"):
        m = LINE.match(line)
        if m:
            g = m.groups()
            rows.append(
                dict(
                    f=int(g[0]),
                    phase=g[1],
                    clock=float(g[2]),
                    home=int(g[3]),
                    away=int(g[4]),
                    ht=int(g[5]),
                    hd=float(g[7]),
                    inn=int(g[8]),
                    parked=int(g[9]),
                    out=int(g[10]),
                    ctl=int(g[11]),
                )
            )
    bad = []
    if len(rows) < 250:
        bad.append(f"only {len(rows)} ms lines (match not reached?)")
    for r in rows:
        # cutscenes (phase Empty: walk-out, replays) may stage players anywhere
        if r["inn"] + r["parked"] + r["out"] != 30 or (r["out"] and r["phase"] != "Empty"):
            bad.append(f"f{r['f']}: players in {r['inn']} parked {r['parked']} out {r['out']}")
        if r["ht"] >= 0 and r["phase"] != "Empty" and r["hd"] > 2.0:
            bad.append(f"f{r['f']}: ball {r['hd']:.2f} m from its holder")
    run = []
    for a, b in zip(rows, rows[1:]):
        if b["clock"] < a["clock"] - 1e-3:
            bad.append(f"f{b['f']}: clock went back {a['clock']} -> {b['clock']}")
        if a["phase"] == b["phase"] == "Free" and b["f"] - a["f"] == 60:
            run.append(b["clock"] - a["clock"])
    steady = sum(1 for d in run if abs(d - 8.8) < 0.05)
    if not run or steady < 0.8 * len(run):
        bad.append(f"clock in open play: {steady} of {len(run)} steps are 8.8 s")
    seen = {r["phase"] for r in rows}
    for p in ("Kickoff", "Free", "Formation", "LineOut", "GoalKick"):
        if p not in seen:
            bad.append(f"phase {p} never seen")
    for r in rows:
        want = (0, 3) if 18000 <= r["f"] < 26800 else (0, 10) if r["f"] >= 28200 else None
        if want and (r["home"], r["away"]) != want:
            bad.append(f"f{r['f']}: score {r['home']}-{r['away']}, HUD {want[0]}-{want[1]}")
    free = [r for r in rows if r["phase"] == "Free"]
    controlled = sum(1 for r in free if r["ctl"] >= 0)
    if not free or controlled < 0.5 * len(free):
        bad.append(f"home controller: a player in {controlled} of {len(free)} open-play lines")
    if bad:
        print(f"match_state_check: FAIL ({len(bad)} problems; first: {bad[0]})")
        return 1
    print(
        f"match_state_check: ok ({len(rows)} samples, phases {','.join(sorted(seen))}, open-play clock "
        f"{steady}/{len(run)} steps of 8.8 s, final score {rows[-1]['home']}-{rows[-1]['away']})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
