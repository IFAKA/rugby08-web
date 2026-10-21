#!/usr/bin/env python3
"""ls.leave: players leave a lockstep party's match and the others play on.

Three pages (headless Chrome, muted, relay on a free port): the host and two friends in a PLAY NOW match
(the host's keyboard and the second friend home, the first friend away). Then:
  friend leaves  the first friend closes its page: the others are told (toast), its controller is
                 unplugged and off its side in both games, the AI takes its player: no reassign screen,
                 the match goes on, the two games stay in sync;
  host leaves    the host closes its page: the room goes on with the second friend (the server hands the
                 room on), whose match goes on (match time advances, nobody waited for), the host's
                 keyboard off its side, and the party's code still joins: a newcomer during the match is
                 told it comes in when the match ends (a match replays slower than it is played), and
                 does when it ends (every game quits it at match time QUIT_AT, RT_MS_QUIT_AT).
One line; details in logs/ls_leave/."""

import os
import re
import sys
import time

from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay, to_menus, wait_screen
from web_route import BACKEND, TEAM_SELECT, browser, play_now, screen

sys.path.insert(0, os.path.dirname(__file__))
from ls_controls_check import controllers  # noqa: E402
from ls_match_check import phases, play  # noqa: E402
from ls_party_check import create, in_sync, join, to_join, type_code  # noqa: E402

D = os.path.join(ROOT, "logs", "ls_leave")
REASSIGN = (0x25E, 0x98, 0x99)
MATCH_MSG = "the party is in a match"  # ui_badge.c: on the party screen's picture or a black one
QUIT_AT = "RT_MS_QUIT_AT=240"  # every game (the same match time: in step) leaves the match then


def match_time(p):
    return p.last(r"ms: pos f \d+ t ([\d.]+)")


def goes_on(players, secs, out, what):
    """The match goes on in every game for secs of play: match time advances, no reassign screen."""
    t0 = [match_time(p) for p in players]
    play(players, secs)
    for p, t in zip(players, t0):
        t1 = match_time(p)
        out.append(f"{what}: {p.name} match time {t:.1f} -> {t1:.1f}")
        if t1 < t + secs / 4:
            return f"{p.name}'s match did not go on ({t:.1f} -> {t1:.1f})"
        if screen(p.lines) in REASSIGN:
            return f"{p.name} shows the reassign screen {screen(p.lines):04X}"
    return None


def into_match(host, f1, f2, out):
    for p in (host, f1, f2):
        err = to_menus(p)
        if err:
            return err
    code = create(host)
    for f in (f1, f2):
        r = join(f, code or "")
        out.append(f"{f.name}: {r}")
        if not r.startswith("caught up"):
            return f"{f.name} did not join: {r}"
    host.key("Escape", 2000)  # back in the main menu
    err = play_now(host.page, host.lines, until=TEAM_SELECT)
    if err or not all(wait_screen(f, TEAM_SELECT, 60) for f in (f1, f2)):
        return f"Team Select not reached by everyone ({err})"
    for p, k in ((host, "ArrowLeft"), (f1, "ArrowRight"), (f2, "ArrowLeft")):
        p.key(k, 2000)
    out.append(f"sides: {controllers(host)}")
    err = play_now(host.page, host.lines, ready=[f1.page, f2.page])
    if err or not all(f.wait(BACKEND, 60) for f in (f1, f2)):
        return f"the match did not start for everyone ({err})"
    play([host, f1, f2], 30)
    return None if phases(host) else "no match numbers"


def run(br, port, out):
    host, f1, f2 = (Player(br, n, port, env=QUIT_AT) for n in ("host", "friend1", "friend2"))
    players = [host, f1, f2]
    try:
        err = into_match(host, f1, f2, out)
        if err:
            return err
        code = host.page.evaluate("Module.rugbyRoom || ''")
        f1.page.close()
        players.remove(f1)
        for p in (host, f2):
            if not p.wait("party toast PLAYER 2 LEFT", 60):
                return f"{p.name} was not told player 2 left"
            if not p.wait_for(lambda p=p: re.search(r"\bP1-", controllers(p)), 30):
                return f"the leaver's pad is still plugged in on {p.name} ({controllers(p)})"
        err = goes_on([host, f2], 30, out, "after the friend left")
        if err:
            return err
        if not in_sync([host, f2], out, "after the friend left"):
            return "the host and the second friend are not in sync"
        host.page.close()
        players.remove(host)
        if not f2.wait("party toast PLAYER 1 LEFT", 60):
            return "the second friend was not told the host left"
        if not f2.wait_for(lambda: "K:H" not in controllers(f2), 30):
            return f"the host's keyboard is still on its side ({controllers(f2)})"
        out.append(f"after the host left: {controllers(f2)}")
        err = goes_on([f2], 30, out, "after the host left")
        if err:
            return err
        newcomer = Player(br, "newcomer", port, env=QUIT_AT)
        players.append(newcomer)
        err = to_menus(newcomer)
        if err:
            return f"newcomer: {err}"
        err = to_join(newcomer)
        if err:
            return f"newcomer: {err}"
        type_code(newcomer, code)
        n = len(newcomer.lines)
        newcomer.key("Enter", 1000)
        # The friend plays on (its set pieces wait for it) while the newcomer joins. Joining replays the
        # whole session (ls.fastjoin is about its speed), so wait while the newcomer gains on the friend:
        # once its replay runs, its frame gap must shrink every 2 minutes, for 45 minutes at most.
        gap, since, end = None, time.time(), time.time() + 45 * 60
        while time.time() < end:
            if any("caught up at frame" in x for x in newcomer.lines[n:]):
                break
            status = [x for x in newcomer.lines[n:] if "ms: joining" in x][-1:]
            started = any("catching up (slot free" in x for x in newcomer.lines[n:])
            if not started or (status and MATCH_MSG in status[0].lower()):
                gap, since = None, time.time()  # still loading the party, or waiting for the match's end
            elif time.time() - since > 120:
                g = f2.last(r"^frame (\d+)") - newcomer.last(r"^frame (\d+)")
                if gap is not None and g >= gap:
                    out.append(f"newcomer: catch-up stopped gaining (frame gap {gap:.0f} -> {g:.0f})")
                    break
                gap, since = g, time.time()
            if [x for x in f2.lines if "ms: game state" in x][-1].split()[3] == BACKEND.split()[-1]:
                play([f2], 1.5)
            else:
                f2.page.wait_for_timeout(1500)
        told = any(MATCH_MSG in x.lower() for x in newcomer.lines[n:])
        joined = any("caught up at frame" in x for x in newcomer.lines[n:])
        out.append(f"newcomer during the match: told it joins after the match {told}, joined after it {joined}")
        if not told or not joined:
            return "a newcomer during the match was not told, or did not join after it"
        play([f2, newcomer], 15)
        return None if in_sync([f2, newcomer], out, "newcomer") else "the newcomer is not in sync"
    finally:
        for p in (host, f1, f2) + tuple(x for x in players if x.name == "newcomer"):
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
    print(f"ls_leave_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
