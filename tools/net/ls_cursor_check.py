#!/usr/bin/env python3
"""ls.cursor: in a lockstep party each screen shows its own player's pointer (runtime/core/ls_cursor.c);
the host's mouse alone drives the shared menus.

Two pages (headless Chrome, muted, relay on a free port): the host makes a party, a friend joins it.
  apart    the host's pointer and the friend's at different places on the party screen: each screen
           draws the game's arrow under its own pointer, both games on the same screen
  together the friend's pointer where the host's is: the two games' pictures (RT_PIX, before the local
           overlays) are equal at the same frames, so the friend's pointer changed nothing in the game
  click    the friend clicks the host's highlighted item: the shared menus stay where they were
One line; details in logs/ls_cursor/."""

import io
import os
import re
import sys

from ls_party_check import create, join
from PIL import Image
from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay, to_menus
from web_route import browser, screen

D = os.path.join(ROOT, "logs", "ls_cursor")
PIX = re.compile(r"pix: frame (\d+) (\S+)")
HOST_AT, FRIEND_AT = (200, 300), (520, 120)  # game units (640x480)


def page_xy(p, gx, gy):
    r = p.page.evaluate(
        "(() => { const b = document.querySelector('canvas').getBoundingClientRect(); "
        "return [b.left, b.top, b.width, b.height]; })()"
    )
    return r[0] + gx * r[2] / 640, r[1] + gy * r[3] / 480


def point(p, at):
    x, y = page_xy(p, *at)
    p.page.mouse.move(x - 4, y - 4)
    p.page.wait_for_timeout(200)
    p.page.mouse.move(x, y)


def arrow_at(p, at, name):
    """Red arrow pixels just right of and below a point of the game's screen (its tip is there)."""
    im = Image.open(io.BytesIO(p.page.screenshot())).convert("RGB")
    im.save(os.path.join(D, name + ".png"))
    x, y = (int(v) for v in page_xy(p, *at))
    px = im.load()
    return sum(
        1
        for j in range(y, min(y + 24, im.size[1]))
        for i in range(x, min(x + 24, im.size[0]))
        if px[i, j][0] > 120 and px[i, j][1] < 80 and px[i, j][2] < 80
    )


def pix(p):
    return {int(m.group(1)): m.group(2) for x in p.lines for m in [PIX.search(x)] if m}


def run(br, port, out):
    host, friend = Player(br, "host", port, env="RT_PIX=10"), Player(br, "friend", port, env="RT_PIX=10")
    try:
        for p in (host, friend):
            err = to_menus(p)
            if err:
                return err
        code = create(host)
        if not code:
            return "CREATE PARTY did not show the party screen with a code"
        r = join(friend, code)
        out.append(f"friend: {r}")
        if not r.startswith("caught up"):
            return f"friend: {r}"
        host.page.wait_for_timeout(8000)
        point(host, HOST_AT)
        point(friend, FRIEND_AT)
        host.page.wait_for_timeout(3000)
        own = [arrow_at(host, HOST_AT, "apart_host"), arrow_at(friend, FRIEND_AT, "apart_friend")]
        other = [arrow_at(host, FRIEND_AT, "apart_host2"), arrow_at(friend, HOST_AT, "apart_friend2")]
        out.append(f"apart: arrow pixels under own pointer host {own[0]} friend {own[1]}, under the other's {other}")
        if min(own) < 15 or max(other) >= 15:
            return "apart: a screen does not draw the arrow under its own pointer (only)"
        if screen(host.lines) != screen(friend.lines):
            return f"apart: the friend shows screen {screen(friend.lines)}, the host {screen(host.lines)}"
        n0 = max(pix(host), default=0)
        point(friend, HOST_AT)
        host.page.wait_for_timeout(4000)
        hp, fp = pix(host), pix(friend)
        common = sorted(f for f in set(hp) & set(fp) if f > n0 + 60)
        bad = [f for f in common if hp[f] != fp[f]]
        out.append(f"together: {len(common)} pictures compared, {len(bad)} different")
        if len(common) < 5 or bad:
            return "together: the two games' pictures differ with both pointers in one place"
        before = screen(host.lines)
        x, y = page_xy(friend, *HOST_AT)
        friend.page.mouse.click(x, y)
        host.page.wait_for_timeout(3000)
        if screen(host.lines) != before or screen(friend.lines) != before:
            return "click: a friend's click changed the shared menus"
        out.append("click: the friend's click changed nothing")
        return None
    finally:
        for p in (host, friend):
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
    print(f"ls_cursor_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
