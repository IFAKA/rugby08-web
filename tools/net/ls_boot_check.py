#!/usr/bin/env python3
"""ls.boot: every web game boots in lockstep with its room open (docs/plans/2026-10-13-lockstep-party.md).

Pages (headless Chrome, muted, relay on a free port), opened as players open the game (no query): the
first page's game starts a session as player 1 with its room created; through its menus into a PLAY
NOW match, its frame rate is measured ("web perf: N fps" lines; >= 55 in the menus and in the match).
A second page then joins that room (its code from the page), catches up and plays along: both games
print the same state hashes ("netplay: frame F hash H": >= 3 frames compared, all equal). One line;
details in logs/ls_boot/."""

import os
import re
import sys
import time

from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay, to_menus
from web_route import browser, play_now

D = os.path.join(ROOT, "logs", "ls_boot")
PERF = re.compile(r"web perf: (\d+) fps")
HASH = re.compile(r"netplay: frame (\d+) hash (\S+)")


def fps(lines, start, end=None):
    v = [int(m.group(1)) for x in lines[start:end] for m in [PERF.search(x)] if m]
    return sorted(v)[len(v) // 2] if v else 0


def hashes(lines):
    return {int(m.group(1)): m.group(2) for x in lines for m in [HASH.search(x)] if m}


def run(br, port, out):
    players = []
    try:
        return steps(br, port, out, players)
    finally:
        for p in players:
            open(os.path.join(D, p.name + ".log"), "w").write("\n".join(p.lines))


def steps(br, port, out, players):
    host = Player(br, "host", port)
    players.append(host)
    if not host.wait("session started as player 1 (created the room)", 120):
        return "the game did not start a session with its room open"
    err = to_menus(host)
    if err:
        return f"host: {err}"
    menus = len(host.lines)
    host.page.wait_for_timeout(20000)
    menu_fps = fps(host.lines, menus)
    err = play_now(host.page, host.lines)
    if err:
        return f"host: {err}"
    match = len(host.lines)
    host.page.wait_for_timeout(30000)
    match_fps = fps(host.lines, match)
    out.append(f"host: {menu_fps} fps in the menus, {match_fps} fps in the match")
    if menu_fps < 55 or match_fps < 55:
        return f"too slow in lockstep (menus {menu_fps} fps, match {match_fps} fps)"
    code = host.page.evaluate("Module.rugbyRoom || ''")
    friend = Player(br, "friend", port, query=f"&room={code}")
    players.append(friend)
    t0 = time.time()
    if not friend.wait("caught up at frame", 900):
        return f"the friend did not catch up with room {code}"
    out.append(f"friend: {friend.wait('caught up at frame', 1)} after {time.time() - t0:.0f} s")
    host.page.wait_for_timeout(20000)
    h, f = hashes(host.lines), hashes(friend.lines)
    both = sorted(set(h) & set(f))
    bad = [k for k in both if h[k] != f[k]]
    out.append(f"{len(both)} hashes compared, {len(bad)} different")
    if len(both) < 3 or bad:
        return f"the games are not in sync ({len(both)} hashes compared, first different at {bad[:1]})"
    return None


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
    print(f"ls_boot_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
