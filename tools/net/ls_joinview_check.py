#!/usr/bin/env python3
"""ls.joinview: joining a party shows the party screen, never a black one (runtime/core/ui_badge.c).

Two pages (headless Chrome, muted, relay on a free port). The host goes ONLINE > CREATE PARTY and stays
on the party screen, so most of a joiner's catch-up replays that screen. The friend joins: while it
catches up, its game draws the party screen now and then with its own row filling up (trace "joining on
the party screen", picture catchup.png): from JOIN PARTY on (its own game shows the party's screen
until the page reloads, then the page's picture of it until the replay gets there, then the game's own),
never a black page (screenshots every 0.4 s), and the host's party screen shows the friend's row filling
up. Once in: each game says "(YOU)" on its own row only, and both
games print the same state hashes (the rows are as long in every game: the game's string heap stays
alike). One line; details in logs/ls_joinview/."""

import io
import os
import sys
import time

from ls_party_check import create, in_sync, to_join, type_code
from PIL import Image, ImageStat
from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay, to_menus
from web_route import browser

D = os.path.join(ROOT, "logs", "ls_joinview")
MINE = {"host": "tag YOU on row 1", "friend": "tag YOU on row 2"}  # each game's own row (drawn: ui_badge.c)


def rows(p):
    return [x.split("party row ", 1)[1].rstrip() for x in p.lines if "party row " in x]


def black(png):  # the game's picture area (crowd, pitch) is black
    g = Image.open(io.BytesIO(png)).convert("L")
    w, h = g.size
    return ImageStat.Stat(g.crop((w * 3 // 10, h * 55 // 100, w * 7 // 10, h * 8 // 10))).mean[0] < 12


def watch(friend, n):
    """Screenshots of the friend's page from JOIN until it caught up: the longest black run in seconds
    (a page between two documents can be black for one shot), None if it never caught up."""
    t0, run, worst, saved = time.time(), 0, 0, set()
    while time.time() - t0 < 600:
        new = friend.lines[n:]
        if any("caught up at frame" in x for x in new):
            return worst if worst >= 0.8 else 0
        try:
            png = friend.page.screenshot(timeout=3000)
        except Exception:  # (the page reloading)
            continue
        if black(png):
            run = run or time.time()
            worst = max(worst, round(time.time() - run, 1))
            if "black" not in saved:
                open(os.path.join(D, "black.png"), "wb").write(png)
                saved.add("black")
        else:
            run = 0
        for name, mark in (("reload", "session started as player 2"), ("catchup", "CATCHING UP WITH")):
            if name not in saved and any(mark in x for x in new):
                open(os.path.join(D, name + ".png"), "wb").write(png)
                saved.add(name)
        friend.page.wait_for_timeout(400)
    return None


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
        err = to_join(friend)
        if err:
            return f"friend: {err}"
        type_code(friend, code)
        t0 = time.time()
        friend.key("Enter", 0)
        dark = watch(friend, n)  # until it caught up: what its page shows, every 0.4 s
        if dark is None:
            return "the friend did not catch up"
        if dark:
            return f"the friend's page was black for {dark} s in a row (black.png)"
        if not os.path.exists(os.path.join(D, "catchup.png")):
            return "the friend's catch-up never showed the party screen"
        out.append("friend: never a black page, the party screen all along (reload.png, catchup.png)")
        xs = {x.rsplit("menu x ", 1)[1].rstrip(")") for x in friend.lines[n:] if "(menu x " in x}
        if len(xs) != 1 or "0" in xs:  # the bar where the game's own menu has it, before and after the reload
            return f"the friend's joining overlay moved: menu x {sorted(xs)}"
        if not any("party screen 0% JOINING THE PARTY" in x for x in friend.lines[n:]):
            return "JOIN PARTY did not show the party's screen before the reload"
        out.append(f"friend: caught up in {time.time() - t0:.0f} s")
        if not any("(player's row)" in x for x in host.lines):
            return "the host's party screen did not show the friend's row filling up"
        out.append("host: the friend's row filled up on its party screen")
        drawn = [x for x in friend.lines[n:] if "joining on a black screen" in x]
        if drawn:
            return f"the friend's game drew the black joining screen ({len(drawn)} times, first {drawn[0]})"
        out.append("never a black screen")
        host.page.wait_for_timeout(8000)
        for p in (host, friend):
            rs = rows(p)
            if [r for r in rs if r.startswith("tag YOU")][-1:] != [MINE[p.name]]:
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
