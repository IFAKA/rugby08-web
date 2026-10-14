#!/usr/bin/env python3
"""ls.own_view: every player of a party sees the match as if playing alone (runtime/core/own_end.c).

Two pages (headless Chrome, muted, relay on a free port), the host's keyboard home, the friend's pad away,
both on the END ON camera (style 3, RT_TEST_CAMERA): the game's one camera turns to whichever team has the
ball (a couch match, measured); each game draws it turned to its own player's end (rt_pov_yaw 180). Two
minutes of play, then:
  ends       both teams had the ball, and the end view each game drew (the camera's way along the pitch,
             reversed while "ms: own end ... turned 180") always looked along its own team's way: the host
             one way, the friend the other, never turning with possession;
  same match the match's state hashes ("netplay: frame F hash") equal in both games (drawing only);
  scheduling every frame's thread switches equal (RT_DET_SCHED).
One line; details in logs/ls_own_view/."""

import os
import re
import sys
from collections import Counter

from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay
from web_route import browser

sys.path.insert(0, os.path.dirname(__file__))
from ls_match_check import into_match, play  # noqa: E402
from ls_party_check import in_sync  # noqa: E402

D = os.path.join(ROOT, "logs", "ls_own_view")
ENV = "RT_TEST_CAMERA=3,RT_MS_CAMSIDE=30,RT_DET_SCHED=1,RT_MS_POS=10"
CAM = re.compile(r"camside f (\d+) cam (.+?) eye (\S+) (\S+) target (\S+) (\S+) ball team (-?\d+)")
TURN = re.compile(r"ms: own end team -?\d+ turned (\d+) at frame (\d+)")
SCHED = re.compile(r"det: frame (\d+) sched (\d+) handoffs \d+ slices (\d+)")


def session(p):
    """This page's last session's lines (a friend's own game before it joined is another)."""
    start = max((i for i, x in enumerate(p.lines) if "netplay: session started" in x), default=0)
    return p.lines[start:]


def ends(lines):
    """The end view drawn (the camera's way along the pitch, reversed while turned) per ball team."""
    turns = sorted((int(m.group(2)), int(m.group(1))) for x in lines for m in [TURN.search(x)] if m)
    seen = Counter()
    for x in lines:
        m = CAM.search(x)
        if not m or not ("Classic" in m.group(2) or "End to End" in m.group(2)):
            continue
        f = int(m.group(1))
        dx, dz = float(m.group(5)) - float(m.group(3)), float(m.group(6)) - float(m.group(4))
        if abs(dz) < abs(dx):
            continue
        turned = next((t for at, t in reversed(turns) if at <= f), 0) == 180
        seen[(("+z" if dz > 0 else "-z") if not turned else ("-z" if dz > 0 else "+z"), int(m.group(7)))] += 1
    return seen


def own_end(seen, name, out):
    """One way while either team had the ball (a few frames of a turning camera aside: 90%)."""
    held = {(w, t): n for (w, t), n in seen.items() if t in (0, 1)}
    teams = {t for (_, t) in held}
    ways = Counter()
    for (w, _), n in held.items():
        ways[w] += n
    total = sum(ways.values())
    main_way, n = ways.most_common(1)[0] if ways else ("?", 0)
    out.append(f"{name}: end camera {main_way} in {n}/{total} samples with the ball held, teams {sorted(teams)}")
    return main_way if teams == {0, 1} and total >= 10 and n >= 0.9 * total else None


def same_sched(players, out):
    s = [
        {int(m.group(1)): (m.group(2), m.group(3)) for x in session(p) for m in [SCHED.match(x)] if m} for p in players
    ]
    common = sorted(set(s[0]) & set(s[1]))
    bad = [f for f in common if s[0][f] != s[1][f]]
    out.append(f"scheduling: {len(common)} frames compared, {len(bad)} different")
    return len(common) > 1000 and not bad


def run(br, port, out):
    host, friend = Player(br, "host", port, env=ENV), Player(br, "friend", port, env=ENV)
    players = [host, friend]
    try:
        err = into_match(host, friend, out)
        if err:
            return err
        play(players, 120)
        ways = [own_end(ends(session(p)), p.name, out) for p in players]
        if None in ways:
            return "a game's end camera turned with possession (or one team never had the ball)"
        if ways[0][1] != ways[1][1] or ways[0] == ways[1]:
            return f"the two games' ends are not opposite ({ways[0]}, {ways[1]})"
        if not in_sync(players, out, "match"):
            return "the two games are not in sync"
        if not same_sched(players, out):
            return "the two games' thread switches differ"
        return None
    finally:
        for p in players:
            open(os.path.join(D, p.name + ".log"), "w").write("\n".join(p.lines))


def main():
    os.makedirs(D, exist_ok=True)
    srv, port = relay(D)
    if not srv:
        return finish("FAIL (relay did not build or start)")
    out, err = [], None
    try:
        with sync_playwright() as pw:
            br = browser(pw)
            err = run(br, port, out)
            br.close()
    finally:
        srv.kill()
        open(os.path.join(D, "steps.log"), "w").write("\n".join(out) + "\n")
    return finish(f"FAIL ({err})" if err else f"ok ({'; '.join(out)})")


def finish(result):
    print(f"ls_own_view_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
