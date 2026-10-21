#!/usr/bin/env python3
"""ls.camera: each player of a party sees the match through their own camera (runtime/core/own_camera.c).

Two pages (headless Chrome, muted, relay on a free port): the host on the game's Side camera, the friend
on Broadcast (RT_TEST_CAMERA=2: the friend's own setting). A PLAY NOW match, the friend's keyboard away.
  same match    the match's state hashes ("netplay: frame F hash", ls_simhash.c) equal in both games;
  own view      their pictures differ (RT_PIX) while the match is the same;
  own controls  the friend runs up its own screen: holding Up, its player moves along its own camera's
                look (within 45 degrees), not along the host's.
One line; details in logs/ls_camera/."""

import math
import os
import re
import sys

from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay
from web_route import browser

sys.path.insert(0, os.path.dirname(__file__))
from ls_match_check import into_match, phases, pixels, play  # noqa: E402
from ls_party_check import in_sync  # noqa: E402

D = os.path.join(ROOT, "logs", "ls_camera")
POS = re.compile(r"ms: pos f (\d+) t \S+ clock \S+ score \S+ phase \S+ upd \S+ us p (.*?) ball")
LOOK = re.compile(r"ms: own camera f (\d+) look (\S+) (\S+) ref \S+ \S+ away (-?\d+)")


def positions(p):
    return {
        int(m.group(1)): [tuple(map(int, xy.split(","))) if xy != "-" else None for xy in m.group(2).split()]
        for x in p.lines
        for m in [POS.search(x)]
        if m
    }


def run_up(friend, host, out):
    """In open play the friend holds Up: its own player runs along the friend's own look."""
    for _ in range(40):
        if phases(host)[-1:] == ["Free"]:
            break
        play([host, friend], 1.5)
    f0 = max(positions(friend) or [0])
    friend.page.keyboard.down("ArrowUp")
    friend.page.wait_for_timeout(2500)  # (a player running the other way turns first: about half a second)
    friend.page.keyboard.up("ArrowUp")
    friend.page.wait_for_timeout(1000)
    ps = positions(friend)
    f1 = max(ps)
    looks = [
        (int(m.group(1)), float(m.group(2)), float(m.group(3)), int(m.group(4)))
        for x in friend.lines
        for m in [LOOK.search(x)]
        if m
    ]
    # The friend's own player (the away side's first human: own_camera.c) at each step, not the AI's: the game
    # hands the friend's control to the player nearest the ball as play moves, so each step counts its own.
    frames = [f for f in sorted(ps) if f0 <= f <= f1]
    dx = dz = 0.0
    for p, q in zip(frames, frames[1:]):
        lk = [x for x in looks if x[0] <= p][-1:]
        if not lk or lk[0][3] < 0 or not ps[p][15 + lk[0][3]] or not ps[q][15 + lk[0][3]]:
            continue
        i = 15 + lk[0][3]
        dx, dz = dx + ps[q][i][0] - ps[p][i][0], dz + ps[q][i][1] - ps[p][i][1]
    look = [x for x in looks if x[0] <= f1][-1:]
    if len(frames) < 2 or not look:
        return f"no measure ({len(frames)} positions, look {bool(look)})"
    i = 15 + look[0][3]
    d = math.hypot(dx, dz)
    ang = math.degrees(math.atan2(dx, dz) - math.atan2(look[0][1], look[0][2]))
    ang = (ang + 180) % 360 - 180
    out.append(
        f"own controls: the friend's players (last {i}) ran {d:.0f} cm at {ang:.0f} degrees from the friend's own look"
    )
    return None if d > 200 and abs(ang) < 45 else "the friend's Up did not run up its own screen"


def run(br, port, out):
    host = Player(br, "host", port, env="RT_PIX=30")
    friend = Player(br, "friend", port, env="RT_PIX=30,RT_TEST_CAMERA=2")
    players = [host, friend]
    try:
        err = into_match(host, friend, out)
        if err:
            return err
        play(players, 20)
        err = run_up(friend, host, out)
        if err:
            return err
        play(players, 30)
        if any("DESYNC" in x for p in players for x in p.lines):
            return "a desync was reported"
        if not in_sync(players, out, "same match"):
            return "the two games are not in step"
        ps = [pixels(p) for p in players]
        common = [f for f in set(ps[0]) & set(ps[1]) if f > max(positions(host) or [0]) - 3000]
        diff = sum(ps[0][f] != ps[1][f] for f in common)
        out.append(f"own view: {diff}/{len(common)} pictures differ")
        return None if common and diff > len(common) // 2 else "the two players do not see their own views"
    finally:
        for p in players:
            open(os.path.join(D, p.name + ".log"), "w").write("\n".join(p.lines))
            try:
                p.page.screenshot(path=os.path.join(D, p.name + ".png"), timeout=10000)
            except Exception as e:  # (a page that hangs must not hide the result)
                out.append(f"{p.name}: no screenshot ({e.__class__.__name__})")


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
    print(f"ls_camera_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
