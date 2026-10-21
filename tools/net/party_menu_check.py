#!/usr/bin/env python3
"""pt.menu: ONLINE in the game's own main menu, the party's screens in its own front end
(runtime/core/fe_screens.c, fe_party.c), with keyboard, mouse and pad; the F2 overlay is gone.

Four pages of the web build (headless Chrome, muted; relay on a free port):
  host     keyboard: ONLINE, CREATE PARTY (its code), CONTINUE: back in the menus with the party badge;
           F2 does nothing; later PLAY NOW's Team Select (its friends' lobby says so)
  typist   keyboard: ONLINE, JOIN PARTY, types the code, Return: in the party's lobby (what the host
           is doing), then LEAVE PARTY: back in its menus, the host's party one smaller
  clicker  mouse only: clicks ONLINE, JOIN PARTY, the code's letters on the grid and OK: in the lobby
  wrong    types a code no party has: the error on its JOIN screen
and a native run (scripted pad, RT_ONLINE=1): ONLINE, JOIN PARTY and a letter of the grid by pad.
Pass: every step above seen in the games' traces. One line; details in logs/party_menu/."""

import os
import re
import subprocess
import sys

from playwright.sync_api import sync_playwright
from web_party import JOIN, MAIN, ONLINE, PARTY, ROOT, Player, relay, wait_screen
from web_party import to_menus as to_main
from web_route import TEAM_SELECT, browser, click, play_now, screen

D = os.path.join(ROOT, "logs", "party_menu")
ABC = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
MENU_X, ITEM_Y0, STEP = 106, 120, 22  # the menus' item window and line spacing (fe_screens.c)


def key_route(p, keys):
    for k in keys:
        p.key(k, 1500)


def web(br, port, out):
    host, typist, clicker, wrong = (Player(br, n, port) for n in ("host", "typist", "clicker", "wrong"))
    players = [host, typist, clicker, wrong]
    try:
        for p in players:
            err = to_main(p)
            if err:
                return err
        if not host.wait("ms: party main menu has ONLINE", 5):
            return "the main menu has no ONLINE item"
        host.key("F2", 2000)
        if host.count("ms: ui screen"):
            return "F2 still opens the old overlay"
        key_route(host, ["ArrowDown", "s"])
        if not wait_screen(host, ONLINE):
            return "host: ONLINE did not open its screen"
        host.key("s", 3000)
        hit = host.wait("as player 1 (host)", 60)
        if not hit or not wait_screen(host, PARTY):
            return "host: CREATE PARTY made no party"
        code = hit.split("ms: ui room ")[1].split()[0]
        host.key("s", 3000)  # CONTINUE
        if not wait_screen(host, MAIN) or not host.wait(f"ms: party badge PARTY {code} 1/4", 20):
            return "host: CONTINUE did not bring it back to its menus with the badge"
        out.append(f"host: party {code}, back in its menus with the badge")
        key_route(wrong, ["ArrowDown", "s", "ArrowDown", "s"] + list("AAAAAA") + ["Enter"])
        if not wrong.wait("ms: party status NO PARTY WITH THAT CODE", 30) or screen(wrong.lines) != JOIN:
            return "a wrong code showed no error on the JOIN screen"
        out.append("wrong code: NO PARTY WITH THAT CODE")
        key_route(typist, ["ArrowDown", "s", "ArrowDown", "s"] + list(code) + ["Enter"])
        if not wait_screen(typist, PARTY, 60) or not typist.wait("ms: party status THE HOST IS IN THE MENUS", 30):
            return "typist: typing the code did not bring it into the party's lobby"
        out.append("typist: in the lobby by keyboard")
        click(clicker.page, MENU_X + 40, ITEM_Y0 + STEP + 10)  # ONLINE
        if not wait_screen(clicker, ONLINE):
            return "clicker: clicking ONLINE did nothing"
        clicker.page.wait_for_timeout(1500)
        click(clicker.page, MENU_X + 40, ITEM_Y0 + STEP + 10)  # JOIN PARTY
        if not wait_screen(clicker, JOIN):
            return "clicker: clicking JOIN PARTY did nothing"
        clicker.page.wait_for_timeout(1500)
        for ch in code:
            i = ABC.index(ch)
            click(clicker.page, MENU_X + 30 * (i % 8) + 10, 168 + STEP * (i // 8) + 10)
            clicker.page.wait_for_timeout(700)
        click(clicker.page, MENU_X + 80 + 15, 168 + STEP * 4 + 6 + 10)  # OK
        if not wait_screen(clicker, PARTY, 60):
            return (
                f"clicker: clicking the code did not join it ({[x for x in clicker.lines if 'party code' in x][-1:]})"
            )
        if not host.wait(f"ms: party badge PARTY {code} 3/4", 30):
            return "the host's badge does not count three players"
        out.append("clicker: in the lobby by mouse; host badge 3/4")
        err = play_now(host.page, host.lines, until=TEAM_SELECT)
        if err or not typist.wait("ms: party status THE HOST IS PICKING TEAMS (PLAY NOW)", 30):
            return f"the lobby does not say the host is picking teams ({err})"
        out.append("lobby: THE HOST IS PICKING TEAMS (PLAY NOW)")
        typist.key("s", 3000)  # LEAVE PARTY (its only button)
        if not wait_screen(typist, MAIN) or not typist.wait("ms: ui leaves room", 5):
            return "typist: LEAVE PARTY did not take it out"
        if not host.wait_for(lambda: any(re.search(r"ui room \w+ members 2$", x) for x in host.lines), 30):
            return "the host's party did not get smaller"
        out.append("typist left; the host's party has 2")
        return None
    finally:
        for p in players:
            open(os.path.join(D, p.name + ".log"), "w").write("\n".join(p.lines))


def native_pad():
    """The scripted pad (slot 0 plugged in by the script) drives the same screens."""
    rep = os.path.join(D, "pad.txt")
    with open(os.path.join(ROOT, "tests", "replays", "skip_to_menu.txt")) as f:
        lines = [x for x in f if x[:1].isdigit()]
    lines += [
        "4600 pad 0 tap down\n",
        "4700 pad 0 tap cross\n",
        "4900 pad 0 tap down\n",
        "5000 pad 0 tap cross\n",
        "5200 pad 0 tap right\n",
        "5300 pad 0 tap cross\n",
    ]
    open(rep, "w").writelines(lines)
    env = dict(os.environ, RT_ONLINE="1", RT_MS_QUIT_AT="1000000", SDL_AUDIODRIVER="dummy")
    with open(os.path.join(D, "pad.out"), "w") as o:
        subprocess.run(
            [
                "timeout",
                "300",
                os.path.join(ROOT, "build", "native", "rugby08"),
                "--frames",
                "5500",
                "--replay",
                rep,
                "--log",
                os.path.join(D, "pad.log"),
            ],
            cwd=ROOT,
            env=env,
            stdout=o,
            stderr=subprocess.STDOUT,
        )
    t = open(os.path.join(D, "pad.out")).read()
    return None if "ms: party code B" in t else "the pad did not pick a letter on JOIN PARTY"


def main():
    os.makedirs(D, exist_ok=True)
    srv, port = relay(D)
    if not srv:
        return finish("FAIL (relay did not build or start)")
    out, err = [], None
    try:
        with sync_playwright() as pw:
            br = browser(pw)
            err = web(br, port, out)
            br.close()
        err = err or native_pad()
    finally:
        srv.kill()
        open(os.path.join(D, "steps.log"), "w").write("\n".join(out) + "\n")
    return finish(
        f"FAIL ({err})"
        if err
        else "ok (ONLINE in the main menu; party made, joined by keyboard, mouse "
        "and pad; wrong code shown; lobby follows the host; leaving works; no F2 overlay)"
    )


def finish(result):
    print(f"party_menu_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
