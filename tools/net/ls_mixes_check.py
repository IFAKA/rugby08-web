#!/usr/bin/env python3
"""ls.mixes: any mix of up to 4 players in a party plays one match, each from their own team's end.

MIX="H,A,H,A" (default: every mix below in turn): one page per letter (headless Chrome, muted, relay on a
free port); the first creates the party, the others join it; at Team Select each moves their own
controller Home (H), Away (A) or leaves it in the middle (M, watching; not the first, whose keyboard the
route puts home). A PLAY NOW match on the END ON camera (style 3), every player pressing Cross now and then
for two minutes. Then:
  sync   the match's state hashes ("netplay: frame F hash") equal in every game, nobody removed;
  ends   (own_end.c) each player of a team saw the end camera along its team's way only (90 % of the
         samples with the ball held), the two teams' ways opposite; a watcher's view never turned.
One line per mix; details in logs/ls_mixes/<mix>/."""

import os
import sys
from collections import Counter

from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay, to_menus, wait_screen
from web_route import BACKEND, TEAM_SELECT, browser, play_now

sys.path.insert(0, os.path.dirname(__file__))
from ls_own_view_check import ends, session  # noqa: E402
from ls_party_check import create, in_sync, join  # noqa: E402

MIXES = ["H,A", "H,H", "H,A,H", "H,A,H,A", "H,H,H,A", "H,H,H,H", "H,A,M"]
ENV = "RT_TEST_CAMERA=3,RT_MS_CAMSIDE=30,RT_MS_POS=10"
KEY = {"H": "ArrowLeft", "A": "ArrowRight"}


def way(p):
    """The way the end view drawn for this player looked (one letter), or None if it turned with the ball."""
    held = Counter()
    for (w, t), n in ends(session(p)).items():
        if t in (0, 1):
            held[w] += n
    total = sum(held.values())
    if not total:
        return "?"
    w, n = held.most_common(1)[0]
    return w if n >= 0.9 * total else None


def into_match(players, sides, out):
    for p in players:
        err = to_menus(p)
        if err:
            return err
    code = create(players[0])
    for p in players[1:]:
        r = join(p, code or "")
        out.append(f"{p.name}: {r}")
        if not r.startswith("caught up"):
            return f"{p.name} did not join: {r}"
    host = players[0]
    host.key("Escape", 2000)
    err = play_now(host.page, host.lines, until=TEAM_SELECT)
    if err or not all(wait_screen(p, TEAM_SELECT, 60) for p in players[1:]):
        return f"Team Select not reached by everyone ({err})"
    for p, s in zip(players[1:], sides[1:]):
        if s in KEY:
            p.key(KEY[s], 2000)
    err = play_now(host.page, host.lines)
    if err or not all(p.wait(BACKEND, 90) for p in players[1:]):
        return f"the match did not start for everyone ({err})"
    return None


def play(players, secs):
    for _ in range(int(secs / 1.5)):
        for p in players:
            p.key("s", 1500 // len(players))


def run_mix(br, mix, out):
    sides = mix.split(",")
    d = os.path.join(ROOT, "logs", "ls_mixes", mix.replace(",", ""))
    os.makedirs(d, exist_ok=True)
    srv, port = relay(d)
    if not srv:
        return "the relay did not build or start"
    players = [Player(br, f"p{i + 1}{s}", port, env=ENV) for i, s in enumerate(sides)]
    try:
        err = into_match(players, sides, out)
        if err:
            return err
        play(players, 120)
        if any("removed from the session" in x or "DESYNC" in x for p in players for x in session(p)):
            return "a game was removed or fell out of step"
        if not in_sync(players, out, f"{mix} match"):
            return "the games are not in sync"
        ways = {p.name: way(p) for p in players}
        out.append(f"{mix} ends: {ways}")
        team = {s: {ways[p.name] for p, x in zip(players, sides) if x == s} for s in "HA"}
        for s in "HA":
            if len(team[s]) > 1 or None in team[s]:
                return f"team {s}'s players did not all see their own end ({ways})"
        if team["H"] and team["A"] and team["H"] == team["A"]:
            return f"the two teams saw the same end ({ways})"
        for p, x in zip(players, sides):
            if x == "M" and any("turned 180" in y for y in session(p)):
                return f"{p.name} watches, yet its view was turned"
        return None
    finally:
        srv.kill()
        for p in players:
            open(os.path.join(d, p.name + ".log"), "w").write("\n".join(p.lines))
            p.page.context.close()


def main():
    mixes = [os.environ["MIX"]] if os.environ.get("MIX") else MIXES
    results = []
    with sync_playwright() as pw:
        br = browser(pw)
        for mix in mixes:
            out = []
            err = run_mix(br, mix, out)
            results.append(f"{mix}: {'FAIL (' + err + ')' if err else 'ok'}")
            print(
                f"ls_mixes_check: {mix}: {'FAIL (' + err + ')' if err else 'ok'} ({'; '.join(out)[:400]})", flush=True
            )
        br.close()
    bad = [r for r in results if "FAIL" in r]
    print(f"ls_mixes_check: {'FAIL' if bad else 'ok'} ({'; '.join(results)})", flush=True)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
