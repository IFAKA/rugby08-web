#!/usr/bin/env python3
"""ls.modes: tournaments and the other multi-player modes in a lockstep party (shared menus, as on a couch).

One run per mode (fresh pages, each in its own browser: a host and a friend; headless Chrome, muted, relay
on a free port). The friend joins the host's party; the host answers the first run's tutorial question (PLAY NOW:
SKIP TUTORIAL and its notes: the modes open, as on a couch), then takes the mode's route in the menus to the fixture's
Team Select (screen 0xF), where both take the human team's side (the CPU's is locked) and the host starts
the match. Every game quits it at match time 10 (RT_MS_QUIT_AT: the same match time, in step), back in
the mode's screens (a tournament's hub); the friend sees the party view there ("THE HOST IS CHOOSING"),
never the host's menus. Then the next fixture, the same way. Pass, per mode: two matches together, the
friend's view back to the party view after each, the games' state hashes equal throughout.
PLAY NOW, the World Cup, Challenge mode and GAME MODES' tournaments (Tri Nations, Six Nations, Super 14,
Premiership, European Trophy).

  tools/net/ls_modes_check.py [MODE ...]     one line; details in logs/ls_modes/"""

import os
import re
import sys

from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay, to_menus, wait_screen
from web_route import BACKEND, TEAM_SELECT, browser, play_now, screen

sys.path.insert(0, os.path.dirname(__file__))
from ls_party_check import create, in_sync, join  # noqa: E402

D = os.path.join(ROOT, "logs", "ls_modes")
QUIT_AT = "RT_MS_QUIT_AT=10"
FIXTURE_TS = 0xF
MAIN_MENU = 0x32
QUESTION = 0x16E  # a popup: the tutorial question and the notes after it
CHOOSING = "ms: party view THE HOST IS CHOOSING"
DOWN = "ArrowDown"
TOURNAMENT = [DOWN] * 4 + ["s", "s"]  # GAME MODES, NEW TOURNAMENT
# Each mode: the keys from the main menu (ONLINE is its second item) to its first screen of fixtures; then
# Cross until the fixture's Team Select (marking a team USER on SELECT TEAMS and starting with Escape).
MODES = {
    "world cup": [DOWN, DOWN, "s", "s", "ArrowRight", "s", "Escape"],
    "challenge": [DOWN, DOWN, DOWN, "s", "s", "s"],
    "tri nations": TOURNAMENT + ["s", "s", "s", "Escape"],
    "six nations": TOURNAMENT + [DOWN, "s", "s", "s", "Escape"],
    "super 14": TOURNAMENT + [DOWN, DOWN, "s", "s", "s", "Escape"],
    "premiership": TOURNAMENT + [DOWN, DOWN, DOWN, "s", "s", "s", "Escape"],
    "european trophy": TOURNAMENT + [DOWN] * 4 + ["s", "s", "s", "Escape"],
}


def sides(p):
    """The players' sides on Team Select (slot order: 'H', 'A' or '-'), as the game has them."""
    for x in reversed(p.lines):
        m = re.search(r"ms: ls sides (\S+)", x)
        if m:
            return m.group(1)
    return ""


def count(p, text):
    return sum(text in x for x in p.lines)


def to_fixture(host):
    """Cross (and the intro screens' accepts) until the fixture's Team Select."""
    for _ in range(40):
        if screen(host.lines) == FIXTURE_TS:
            return True
        host.key("s", 2500)
    return False


def unlock(host):
    """PLAY NOW's first-run question (SKIP TUTORIAL, its notes, CONTINUE WITHOUT SAVING): back in the main
    menu with the modes open."""
    for _ in range(4):  # (the menu, just back from the party screen, may not take the first Cross)
        host.key("s", 2500)
        if wait_screen(host, QUESTION, 10):
            break
    else:
        return f"no tutorial question on PLAY NOW (screen {screen(host.lines)})"
    for _ in range(8):
        host.key("s", 2500)
        if screen(host.lines) == MAIN_MENU:
            return None
    return f"the tutorial question's popups did not close (screen {screen(host.lines)})"


def take_sides(host, friend, together):
    """Both on a side: the same one in a fixture (the human team's: the CPU's is locked, so try home,
    then away), any in PLAY NOW (both teams are open)."""
    for k in ("ArrowLeft", "ArrowRight", "ArrowRight"):
        host.key(k, 1500)
        friend.key(k, 2500)
        s = sides(host)[:2]
        if "-" not in s and len(s) == 2 and (s[0] == s[1] or not together):
            return s
    return sides(host)


def one_match(host, friend, n, out, name, together):
    """From Team Select into the match, out again at match time 10, the friend in the party view."""
    s = take_sides(host, friend, together)
    if len(s) < 2 or "-" in s[:2] or (together and s[0] != s[1]):
        return f"{name}: the players did not take their sides (sides {s})"
    starts, quits = count(friend, BACKEND), count(host, "ms: quit at frame")
    for _ in range(12):  # CONTINUE (and whatever the mode shows before the match)
        if count(host, BACKEND) > starts or screen(host.lines) == 0:
            break
        host.key("s", 2500)
    if not friend.wait_for(lambda: count(friend, BACKEND) > starts, 120):
        return f"{name}: the friend's game did not start match {n}"
    for _ in range(160):  # the intro and the set pieces wait for answers: each side answers its own
        if count(host, "ms: quit at frame") > quits and count(friend, "ms: quit at frame") > quits:
            break
        host.key("s", 750)
        friend.key("s", 750)
    else:
        return f"{name}: match {n} did not reach match time 10 on both"
    q = max(i for i, x in enumerate(friend.lines) if "ms: quit at frame" in x)
    if not friend.wait_for(lambda: any(CHOOSING in x for x in friend.lines[q:]), 120):
        return f"{name}: after match {n} the friend does not see the party view (screen {screen(friend.lines)})"
    out.append(f"{name}: match {n} together (sides {s}), the friend back in the party view")
    return None


def run_mode(brs, port, name, out):
    host = Player(brs[0], "host", port, env=QUIT_AT)
    friend = Player(brs[1], "friend", port, env=QUIT_AT)
    try:
        for p in (host, friend):
            err = to_menus(p)
            if err:
                return f"{name}: {err}"
        r = join(friend, create(host) or "")
        if not r.startswith("caught up"):
            return f"{name}: the friend did not join: {r}"
        host.key("s", 2000)  # the party screen's CONTINUE: the main menu, on its first item
        if not wait_screen(host, MAIN_MENU, 30):
            return f"{name}: the host is not back in the main menu (screen {screen(host.lines)})"
        if name == "play now":
            err = play_now(host.page, host.lines, until=TEAM_SELECT)
            if err:
                return f"{name}: {err}"
            err = one_match(host, friend, 1, out, name, False)
        else:
            err = unlock(host)
            if err:
                return f"{name}: {err}"
            for k in MODES[name]:
                host.key(k, 2500)
            if not to_fixture(host):
                return f"{name}: the host's route did not reach a fixture's Team Select (screen {screen(host.lines)})"
            err = one_match(host, friend, 1, out, name, True)
            if not err and not to_fixture(host):  # the next fixture (a hub's NEXT MATCH, ...)
                err = f"{name}: no next fixture after match 1 (screen {screen(host.lines)})"
            err = err or one_match(host, friend, 2, out, name, True)
        if not err and not in_sync([host, friend], out, name):
            err = f"{name}: the games are not in sync"
        return err
    finally:
        tag = name.replace(" ", "_")
        for p in (host, friend):
            p.page.screenshot(path=os.path.join(D, f"{tag}_{p.name}.png"))
            open(os.path.join(D, f"{tag}_{p.name}.log"), "w").write("\n".join(p.lines))
            p.page.context.close()


def main(argv):
    names = [a for a in argv if a in MODES or a == "play now"] or ["play now"] + list(MODES)
    os.makedirs(D, exist_ok=True)
    srv, port = relay(D)
    if not srv:
        return finish("FAIL (relay did not build or start)")
    out, fails = [], []
    try:
        with sync_playwright() as pw:
            brs = [browser(pw), browser(pw)]  # a browser each, as on two machines (one GPU process each)
            for name in names:
                err = run_mode(brs, port, name, out)
                if err:
                    fails.append(err)
                    out.append(f"FAIL {err}")
            for b in brs:
                b.close()
    finally:
        srv.kill()
        open(os.path.join(D, "modes.log"), "w").write("\n".join(out) + "\n")
    ok = len(names) - len(fails)
    return finish(f"FAIL ({ok}/{len(names)} modes; {'; '.join(fails)})" if fails else f"ok ({ok}/{len(names)} modes)")


def finish(result):
    print(f"ls_modes_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
