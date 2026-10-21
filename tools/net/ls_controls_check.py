#!/usr/bin/env python3
"""ls.controls: each player drives their own controller in the lockstep party (runtime/core/pads.c).

Two pages (headless Chrome, muted, relay on a free port). Solo, the host's game has only its own
controllers (mouse and keyboard; every pad slot unplugged: "ms: controllers M K P0- P1- P2- P3-"). A
friend joins by code (ONLINE > JOIN PARTY): from then on both games show pad 1 plugged in. The host
takes PLAY NOW to Team Select; the host presses Left on its keyboard, the friend Right on its own
keyboard (its controller): both games show the keyboard at home and pad 1 away. The friend then leaves
(Esc back out and LEAVE PARTY is not needed: closing its page leaves): pad 1 is unplugged in the host's
game. One line; details in logs/ls_controls/."""

import os
import re
import sys

from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay, to_menus, wait_screen
from web_route import TEAM_SELECT, browser, play_now

sys.path.insert(0, os.path.dirname(__file__))
from ls_party_check import create, join  # noqa: E402

D = os.path.join(ROOT, "logs", "ls_controls")


def controllers(p):
    for x in reversed(p.lines):
        m = re.match(r"ms: controllers (.*) at frame", x)
        if m:
            return m.group(1)
    return ""


def run(br, port, out):
    host, friend = Player(br, "host", port), Player(br, "friend", port)
    players = [host, friend]
    try:
        for p in players:
            err = to_menus(p)
            if err:
                return err
        solo = controllers(host)
        out.append(f"solo: {solo}")
        if re.search(r"P\d(?!-)", solo):
            return f"a solo game has pads plugged in ({solo})"
        code = create(host)
        r = join(friend, code or "")
        out.append(f"friend: {r}")
        if not r.startswith("caught up"):
            return f"the friend did not join: {r}"
        host.key("Escape", 2000)  # out of the party screen, back in the main menu
        if not host.wait_for(lambda: re.search(r"\bP1(?!-)", controllers(host)), 30):
            return f"the friend's pad is not plugged in on the host ({controllers(host)})"
        err = play_now(host.page, host.lines, until=TEAM_SELECT)
        if err or not wait_screen(friend, TEAM_SELECT, 60):
            return f"Team Select not reached on both ({err})"
        host.key("ArrowLeft", 2000)
        friend.key("ArrowRight", 3000)
        for p in players:
            c = controllers(p)
            out.append(f"{p.name}: {c}")
            if "K:H" not in c or "P1:A" not in c:
                return f"{p.name}'s game does not show the keyboard home and pad 1 away ({c})"
        friend.page.close()
        if not host.wait_for(lambda: re.search(r"\bP1-", controllers(host)), 60):
            return f"the leaver's pad is still plugged in ({controllers(host)})"
        out.append(f"after the leave: {controllers(host)}")
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
    print(f"ls_controls_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
