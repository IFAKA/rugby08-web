#!/usr/bin/env python3
"""ls.joinview: joining a party shows the party screen, never a black one (runtime/core/ui_badge.c).

Two pages (headless Chrome, muted, relay on a free port). The host goes ONLINE > CREATE PARTY and stays
on the party screen, so most of a joiner's catch-up replays that screen. The friend joins: while it
catches up, its game draws the party screen now and then with its own row filling up (trace "joining on
the party screen", picture catchup.png): from JOIN PARTY on (its own game shows the party's screen
until the page reloads, then the page's picture of it until the replay gets there, then the game's own),
never the black joining screen. Once in: each game says "(YOU)" on its own row only, and both
games print the same state hashes (the rows are as long in every game: the game's string heap stays
alike). One line; details in logs/ls_joinview/."""

import os
import sys
import time

from ls_party_check import create, in_sync, to_join, type_code
from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay, to_menus
from web_route import browser

D = os.path.join(ROOT, "logs", "ls_joinview")
MINE = {"host": "1  PLAYER 1 (HOST) (YOU)", "friend": "2  PLAYER 2 (YOU)"}  # each game's own row


def rows(p):
    return [x.split("party row ", 1)[1].rstrip() for x in p.lines if "party row " in x]


def run(br, port, out):
    host, friend = Player(br, "host", port), Player(br, "friend", port)
    try:
        for p in (host, friend):
            err = to_menus(p)
            if err:
                return err
        code = create(host)
        if not code:
            return "CREATE PARTY did not show the party screen with a code"
        out.append(f"host: party {code}")
        host.page.wait_for_timeout(40000)  # the party screen: most of what the friend replays
        friend.page.screenshot(path=os.path.join(D, "friend_menu.png"))
        n = len(friend.lines)
        t0 = time.time()
        shot = []

        def seen():  # the joining screen over the party screen: a picture of it, once
            hit = any("joining on the party screen" in x for x in friend.lines[n:])
            if hit and not shot:
                friend.page.screenshot(path=os.path.join(D, "catchup.png"))
                shot.append(next(x for x in friend.lines[n:] if "joining on the party screen" in x))
            return hit or any("caught up at frame" in x for x in friend.lines[n:])

        err = to_join(friend)
        if err:
            return f"friend: {err}"
        type_code(friend, code)
        friend.key("Enter", 1000)
        friend.wait_for(seen, 600)
        if not shot:
            return "the friend's catch-up never showed the party screen"
        out.append(f"friend: catching up over the party screen ({shot[0].split('screen ')[-1]})")
        if not any("party screen 0% JOINING THE PARTY" in x for x in friend.lines[n:]):
            return "JOIN PARTY did not show the party's screen before the reload"
        if not friend.wait_for(lambda: any("caught up at frame" in x for x in friend.lines[n:]), 600):
            return "the friend did not catch up"
        out.append(f"friend: caught up in {time.time() - t0:.0f} s")
        black = [x for x in friend.lines[n:] if "joining on a black screen" in x]
        if black:
            return f"the friend showed the black joining screen ({len(black)} times, first {black[0]})"
        out.append("never a black screen")
        host.page.wait_for_timeout(8000)
        for p in (host, friend):
            rs = rows(p)
            if MINE[p.name] not in rs or any("(YOU)" in r and r != MINE[p.name] for r in rs):
                return f"the {p.name}'s rows: {rs[-4:]}"
        out.append("(YOU) on each game's own row")
        if not in_sync([host, friend], out, "two players"):
            return "the two games are not in sync"
        return None
    finally:
        for p in (host, friend):
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
    print(f"ls_joinview_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
