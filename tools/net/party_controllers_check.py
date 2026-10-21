#!/usr/bin/env python3
"""pt.controllers: the game's controllers follow the party (runtime/core/pad_attach.c).

Four pages of the web build (headless Chrome, muted, on a relay at a free port). The host plays its
menus to PLAY NOW's Team Select alone: only its own devices are there (no virtual pads). It hosts a
party from its main menu (ONLINE, CREATE PARTY), back in Team Select three friends join (ONLINE,
JOIN PARTY): each one adds a
controller to the host's Team Select. Each player moves its own icon with its own keys (host Left,
friends Right / Left / Right): all four take a side. The last friend closes its page: its controller
goes, off its side. The host's "ms: controllers" trace lines are the game's own controller list (entry
order, plugged in or not "-", side ":H"/":A"); a screenshot of Team Select with four icons is kept.

  tools/net/party_controllers_check.py        one line; details in logs/party_controllers/"""

import os
import re
import sys
import time

from playwright.sync_api import sync_playwright
from web_party import MAIN, ROOT, Player, host_room, join_room, relay, to_menus
from web_route import TEAM_SELECT, browser, play_now, screen, tap

D = os.path.join(ROOT, "logs", "party_controllers")


def controllers(p):
    """The host's newest controller list: [(kind, plugged, side)], entries 0..8."""
    for x in reversed(p.lines):
        m = re.search(r"ms: controllers (.*) at frame", x)
        if m:
            return [
                (t.rstrip("-").split(":")[0], not t.split(":")[0].endswith("-"), t.split(":")[1] if ":" in t else "")
                for t in m.group(1).split()
            ]
    return []


def side_slots(p):
    """Entries 1..4 (the ones Team Select offers): their plugged devices and sides."""
    return [c for c in controllers(p)[1:5] if c[1]]


def wait_for(p, cond, secs):
    end = time.time() + secs
    while time.time() < end:
        if cond():
            return True
        p.page.wait_for_timeout(500)
    return False


def run(players, out):
    host, friends = players[0], players[1:]
    if not host.wait("ms: game state 008CD620", 600):
        return "the host never reached its menus"
    host.page.locator("canvas").click()
    err = play_now(host.page, host.lines, until=TEAM_SELECT)
    if err:
        return f"host: {err}"
    host.page.wait_for_timeout(3000)
    solo = side_slots(host)
    out.append(f"solo: {controllers(host)}")
    if [c[0] for c in solo] != ["K"]:
        return f"solo Team Select offers {solo}, not only the keyboard"
    for _ in range(6):  # back to the main menu (Triangle), to make the party there
        if screen(host.lines) == MAIN:
            break
        host.key("w", 2500)
    code = host_room(host)
    if not code:
        return "no party"
    err = play_now(host.page, host.lines, until=TEAM_SELECT)
    if err:
        return f"host, back to Team Select: {err}"
    for n, f in enumerate(friends, 1):
        err = to_menus(f)
        if err:
            return err
        if not join_room(f, code):
            return f"{f.name} did not join {code}"
        if not wait_for(host, lambda n=n: len(side_slots(host)) == 1 + n, 30):
            return f"after {f.name} joined the host's Team Select offers {side_slots(host)}"
        out.append(f"{f.name} joined: {controllers(host)}")
    for p, k in zip(players, ["ArrowLeft", "ArrowRight", "ArrowLeft", "ArrowRight"]):
        tap(p.page, k, 300)
        p.page.wait_for_timeout(1500)
    if not wait_for(host, lambda: sorted(c[2] for c in side_slots(host)) == ["A", "A", "H", "H"], 30):
        return f"not every player took a side: {side_slots(host)}"
    out.append(f"sides: {controllers(host)}")
    host.page.screenshot(path=os.path.join(D, "team_select_4.png"))
    gone = friends[-1]
    gone.page.close()
    if not wait_for(host, lambda: len(side_slots(host)) == 3, 60):
        return f"the leaver's controller stayed: {controllers(host)}"
    left = [c for c in controllers(host) if c[0] == "P3"]
    if not left or left[0][1] or left[0][2]:
        return f"the leaver's controller is still plugged in or on a side: {left}"
    out.append(f"friend3 left: {controllers(host)}")
    return None


def main():
    os.makedirs(D, exist_ok=True)
    srv, port = relay(D)
    if not srv:
        return finish("FAIL (relay did not start)")
    out, result = [], "FAIL (setup)"
    try:
        with sync_playwright() as pw:
            br = browser(pw)
            players = [Player(br, n, port) for n in ("host", "friend1", "friend2", "friend3")]
            try:
                err = run(players, out)
                result = (
                    f"FAIL ({err})"
                    if err
                    else (
                        "ok (solo: the keyboard only; 3 friends joining added 3 controllers, all 4 took a side, "
                        "the leaver's controller went)"
                    )
                )
            finally:
                for p in players:
                    open(os.path.join(D, p.name + ".log"), "w").write("\n".join(p.lines))
                br.close()
    finally:
        srv.kill()
        open(os.path.join(D, "steps.log"), "w").write("\n".join(out) + "\n")
    return finish(result)


def finish(result):
    print(f"party_controllers_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
