#!/usr/bin/env python3
"""pt.sides: Team Select on every party member's own screen, in sync (runtime/core/party_sides.c).

Three pages (host, two friends; headless Chrome, muted, relay on a free port) form a party; the host
goes to PLAY NOW's Team Select and its friends' games follow it there. Each player moves its own icon
(host Left = home, both friends Right = away: the first friend is the away side's captain). The second
friend's Down (change its side's team) is refused; the captain's changes the away team; the host's
changes the home team; a friend's Cross (CONTINUE) does nothing; every friend's screen shows the host's
sides, teams and kits ("ms: sides" traces) and only the party's controllers. The host's Cross goes on
to the stadium and its friends go back to their lobby. One line; details in logs/party_sides/."""

import os
import re
import sys

from playwright.sync_api import sync_playwright
from web_party import PARTY, ROOT, Player, host_room, join_room, relay, to_menus, wait_screen
from web_route import TEAM_SELECT, browser, play_now, screen

D = os.path.join(ROOT, "logs", "party_sides")


def sides(p, who):
    """The newest "ms: sides WHO ..." state: (sides, home team, away team)."""
    for x in reversed(p.lines):
        m = re.match(rf"ms: sides {who} (\S+) home [^/]+/ ([^/]+) / [^/]+ away [^/]+/ ([^/]+) /", x)
        if m:
            return m.group(1), m.group(2).strip(), m.group(3).strip()
    return None


def run(players, out):
    host, f1, f2 = players
    for p in players:
        err = to_menus(p)
        if err:
            return err
    code = host_room(host)
    if not code or not join_room(f1, code) or not join_room(f2, code):
        return "no party of three"
    err = play_now(host.page, host.lines, until=TEAM_SELECT)
    if err:
        return f"host: {err}"
    for f in (f1, f2):
        if not wait_screen(f, TEAM_SELECT, 30):
            return f"{f.name} did not follow the host into Team Select"
    host.page.wait_for_timeout(2000)
    for p, k in ((host, "ArrowLeft"), (f1, "ArrowRight"), (f2, "ArrowRight")):
        p.key(k, 2000)
    if not host.wait_for(lambda: (sides(host, "host") or ("",))[0] == "HAA-", 30):
        return f"the icons did not take their sides: {sides(host, 'host')}"
    before = sides(host, "host")
    out.append(f"sides HAA-: {before}")
    f2.key("ArrowDown", 3000)  # not the away captain
    if sides(host, "host")[2] != before[2] or not host.wait("is not side A's captain", 5):
        return "the second away player changed the away team"
    f1.key("ArrowDown", 3000)  # the captain
    if not host.wait_for(lambda: sides(host, "host")[2] != before[2], 10):
        return "the away captain could not change the away team"
    host.key("ArrowDown", 3000)
    if not host.wait_for(lambda: sides(host, "host")[1] != before[1], 10):
        return "the host could not change the home team"
    now = sides(host, "host")
    out.append(f"captains: away {before[2]} -> {now[2]} (player 2), home {before[1]} -> {now[1]} (host)")
    for f in (f1, f2):
        if not f.wait_for(lambda f=f: sides(f, "friend") == now, 20):
            return f"{f.name}'s Team Select shows {sides(f, 'friend')}, the host's {now}"
        ctl = [x for x in f.lines if x.startswith("ms: controllers")][-1:]
        if not ctl or not re.search(r"K:H P1:A P2:A", ctl[0]):
            return f"{f.name}'s controllers are not the party's: {ctl}"
    out.append("both friends show the host's Team Select and the party's controllers")
    f1.key("s", 3000)  # a friend's CONTINUE: nothing
    if screen(host.lines) != TEAM_SELECT:
        return "a friend's Cross took the host out of Team Select"
    host.key("s", 3000)
    if not host.wait_for(lambda: screen(host.lines) == 7, 30):
        return "the host's CONTINUE did not reach the stadium"
    for f in (f1, f2):
        if not wait_screen(f, PARTY, 30):
            return f"{f.name} did not go back to its lobby"
    out.append("host on to the stadium, friends back in their lobby")
    host.page.screenshot(path=os.path.join(D, "host.png"))
    return None


def main():
    os.makedirs(D, exist_ok=True)
    srv, port = relay(D)
    if not srv:
        return finish("FAIL (relay did not build or start)")
    out, err = [], "setup"
    try:
        with sync_playwright() as pw:
            br = browser(pw)
            players = [Player(br, n, port) for n in ("host", "friend1", "friend2")]
            try:
                err = run(players, out)
            finally:
                for p in players:
                    open(os.path.join(D, p.name + ".log"), "w").write("\n".join(p.lines))
                br.close()
    finally:
        srv.kill()
        open(os.path.join(D, "steps.log"), "w").write("\n".join(out) + "\n")
    return finish(
        f"FAIL ({err})"
        if err
        else "ok (Team Select on all three screens in sync; own icons; captains "
        "change their side's team; only the host continues; friends back to their lobby)"
    )


def finish(result):
    print(f"party_sides_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
