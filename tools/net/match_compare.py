#!/usr/bin/env python3
"""Compare two games playing the same match: the host's and a friend's "ms: pos" lines (RT_MS_POS,
runtime/core/match_measure.c), lined up by match time t (the host logging every frame, RT_MS_POS=1, gives
every friend line a partner). Prints one summary line; every aligned pair goes to logs/match_compare.log.

  tools/net/match_compare.py HOST.out FRIEND.out [--tol CM] [--from T] [--to T]

Per pair: the worst and median player distance (cm, players on the pitch in both), the ball distance,
and whether clock, score and phase agree. Exit 0 when every pair is within --tol (default 50 cm) with the
same score and phase."""

import bisect
import os
import re
import statistics
import sys

POS = re.compile(
    r"ms: pos f (\d+) t ([-\d.]+) clock ([-\d.]+) score (\d+)-(\d+) phase (\S+) upd (\d+)/(\d+) us p(.*?) ball (\S+)(?: pup dt \S+ worst (\d+) cm heading (\S+) blocks (\d+)/30 ball (\d+) cm)?"
)
PARKED = 9000  # cm: players parked off the pitch for a cutscene (95.25 m)


def parse(path):
    out = []
    for line in open(path, errors="replace"):
        m = POS.search(line)
        if not m:
            continue
        players = [None if s == "-" else tuple(map(int, s.split(","))) for s in m.group(9).split()]
        out.append(
            {
                "f": int(m.group(1)),
                "t": float(m.group(2)),
                "clock": float(m.group(3)),
                "score": (int(m.group(4)), int(m.group(5))),
                "phase": m.group(6),
                "upd": (int(m.group(7)), int(m.group(8))),
                "p": players,
                "ball": tuple(map(int, m.group(10).split(","))),
                # the friend's own recorder against the host frame (match_puppet.c ms_puppet_check)
                "pup": None
                if m.group(11) is None
                else (int(m.group(11)), float(m.group(12)), int(m.group(13)), int(m.group(14))),
            }
        )
    return out


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def compare(h, f):
    ds = [dist(a, b) for a, b in zip(h["p"], f["p"]) if a and b and abs(a[0]) < PARKED and abs(b[0]) < PARKED]
    return {
        "worst": max(ds) if ds else 0.0,
        "median": statistics.median(ds) if ds else 0.0,
        "n": len(ds),
        "ball": dist(h["ball"], f["ball"]),
        "clock": abs(h["clock"] - f["clock"]),
        "same": h["score"] == f["score"] and h["phase"] == f["phase"],
    }


def main(argv):
    if len(argv) < 2:
        sys.exit(__doc__)
    opt = {"--tol": 50.0, "--from": -1e9, "--to": 1e9}
    args, i = [], 0
    while i < len(argv):
        if argv[i] in opt:
            opt[argv[i]] = float(argv[i + 1])
            i += 2
        else:
            args.append(argv[i])
            i += 1
    host, friend = parse(args[0]), parse(args[1])
    if not host or not friend:
        print(f"match_compare: FAIL (no ms: pos lines in {'host' if not host else 'friend'})")
        return 1
    ts = [h["t"] for h in host]
    os.makedirs("logs", exist_ok=True)
    pairs, first_bad, unmatched = [], None, 0
    with open("logs/match_compare.log", "w") as log:
        for fl in friend:
            if not opt["--from"] <= fl["t"] <= opt["--to"]:
                continue
            k = bisect.bisect_left(ts, fl["t"])
            cand = [j for j in (k - 1, k) if 0 <= j < len(host)]
            j = min(cand, key=lambda j: abs(ts[j] - fl["t"]))
            if abs(ts[j] - fl["t"]) > 0.011:
                unmatched += 1
                continue
            c = compare(host[j], fl)
            pairs.append(c)
            bad = c["worst"] > opt["--tol"] or not c["same"]
            if bad and first_bad is None:
                first_bad = (fl, host[j], c)
            log.write(
                f"t {fl['t']:.3f} host f {host[j]['f']} friend f {fl['f']} worst {c['worst']:.0f} median "
                f"{c['median']:.0f} cm ({c['n']} players) ball {c['ball']:.0f} cm clock {c['clock']:.2f} "
                f"score {host[j]['score']}/{fl['score']} phase {host[j]['phase']}/{fl['phase']} "
                f"upd {fl['upd'][0]}/{fl['upd'][1]} us"
                + (
                    f" rec worst {fl['pup'][0]} cm heading {fl['pup'][1]} blocks {fl['pup'][2]}/30 ball {fl['pup'][3]} cm"
                    if fl["pup"]
                    else ""
                )
                + f"{' BAD' if bad else ''}\n"
            )
    if not pairs:
        print(f"match_compare: FAIL (no friend line has a host line at its match time; {unmatched} unmatched)")
        return 1
    worst = max(p["worst"] for p in pairs)
    med = statistics.median(p["median"] for p in pairs)
    ball = max(p["ball"] for p in pairs)
    upd = statistics.mean(fl["upd"][0] for fl in friend)
    rec = [fl["pup"] for fl in friend if fl["pup"]]
    if rec:  # recorder-level: what the friend's game itself recorded, i.e. what it shows
        upd_note = f", recorded: players worst {max(r[0] for r in rec)} cm, blocks min {min(r[2] for r in rec)}/30, ball worst {max(r[3] for r in rec)} cm"
    else:
        upd_note = ""
    summary = (
        f"{len(pairs)} aligned ({unmatched} unmatched), players worst {worst:.0f} cm median {med:.0f} cm, "
        f"ball worst {ball:.0f} cm, friend update {upd:.0f} us{upd_note}"
    )
    if first_bad:
        fl, h, c = first_bad
        print(
            f"match_compare: FAIL ({summary}; first off at t {fl['t']:.2f}: worst {c['worst']:.0f} cm, score "
            f"{h['score']}/{fl['score']}, phase {h['phase']}/{fl['phase']}; logs/match_compare.log)"
        )
        return 1
    print(f"match_compare: ok ({summary})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
