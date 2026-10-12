#!/usr/bin/env python3
"""on.match_setup evaluation (tools/net/match_setup_check.sh): DIR holds host/own/friend runs, each with
an "ms: launch" and an "ms: after launch" line and a screenshot K frames after its launch.
Prints one summary line; exit 0 = the friend plays the host's match."""

import re
import sys

from PIL import Image, ImageChops, ImageStat

LAUNCH = re.compile(r"ms: launch setup (\w+) \(own (\w+)\) teams (\d+) v (\d+) stadium (\d+)")
AFTER = re.compile(r"ms: after launch \d+ setup (\w+) teams (\d+) v (\d+) stadium (\d+) phase (\S+)")


def parse(d, name):
    launch = after = None
    for line in open(f"{d}/{name}.out", errors="replace"):
        launch = launch or LAUNCH.match(line)
        after = after or AFTER.match(line)
    return launch, after


def mean_diff(a, b):
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    return sum(ImageStat.Stat(ImageChops.difference(ia, ib)).mean) / 3


def main(d):
    runs = {n: parse(d, n) for n in ("host", "own", "friend")}
    for n, (launch, after) in runs.items():
        if not launch or not after:
            print(f"match_setup_check: FAIL ({n}: {'no launch' if not launch else 'no after-launch line'})")
            return 1
    host, own, friend = (runs[n][1].groups() for n in ("host", "own", "friend"))
    fl = runs["friend"][0].groups()
    bad = []
    if own[1:4] == host[1:4]:
        bad.append(f"the friend's own route chose the host's match already (teams {own[1]} v {own[2]})")
    if fl[1] == fl[0]:
        bad.append("the friend's launch did not take the host's setup")
    if friend[0] != host[0]:
        bad.append(f"setup bytes differ after launch (friend {friend[0]}, host {host[0]})")
    if friend[1:4] != host[1:4]:
        bad.append(
            f"friend teams {friend[1]} v {friend[2]} stadium {friend[3]}, host {host[1]} v {host[2]} stadium {host[3]}"
        )
    same = mean_diff(f"{d}/friend.png", f"{d}/host.png")
    other = mean_diff(f"{d}/own.png", f"{d}/host.png")
    if not same < other / 3:
        bad.append(f"picture: friend vs host {same:.1f}, friend's own match vs host {other:.1f}")
    if bad:
        print(f"match_setup_check: FAIL ({bad[0]})")
        return 1
    print(
        f"match_setup_check: ok (friend chose teams {own[1]} v {own[2]} stadium {own[3]}, played the host's "
        f"{host[1]} v {host[2]} stadium {host[3]}, setup {host[0]}; picture diff {same:.1f} vs {other:.1f} for its own)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
