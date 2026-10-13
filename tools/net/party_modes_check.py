#!/usr/bin/env python3
"""pt.modes: every game mode that lets several players play works in a party, between matches too.

One run per mode (fresh pages: a host and a friend; headless Chrome, muted, relay on a free port). The
host makes the party, answers the first-run tutorial question (which unlocks the modes, as for any new
player), takes the mode's route in its own menus to the fixture's Team Select (screen 0xF), where both
take the human team's side (the CPU side is locked), and starts the match; the friend follows it in from
its lobby. The host quits at match time 10 (RT_MS_QUIT_AT) as a player would: it is back in the mode's
screens (a tournament's hub), the friend back in the party's lobby, told what the host is doing; then the
host plays the next fixture and the friend follows again. PLAY NOW, the World Cup, Challenge mode and the
tournaments of GAME MODES (Tri Nations, Six Nations, Super 14, Premiership, European Trophy).

  tools/net/party_modes_check.py [MODE ...]     one line; details in logs/party_modes/"""

import os
import re
import sys

from playwright.sync_api import sync_playwright
from web_party import PARTY, ROOT, Player, host_room, join_room, relay, to_menus, wait_screen
from web_route import TEAM_SELECT, browser, play_now, screen

D = os.path.join(ROOT, "logs", "party_modes")
FIXTURE_TS = 0xF
UNLOCK = ["s", "ArrowRight", "s", "s", "s", "s"]  # PLAY NOW's tutorial question: no, continue: back in the menu
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
    for x in reversed(p.lines):
        m = re.match(r"ms: sides host (\S+)", x)
        if m:
            return m.group(1)
    return ""


def to_fixture(host):
    """Cross (and the intro screens' accepts) until the fixture's Team Select."""
    for _ in range(40):
        if screen(host.lines) == FIXTURE_TS:
            return True
        host.key("s", 2500)
    return False


def take_sides(host, friend):
    """Both on the human team's side (the CPU's is locked: try home, then away)."""
    for k in ("ArrowLeft", "ArrowRight"):
        host.key(k, 1500)
        friend.key(k, 2500)
        s = sides(host)
        if s[:2] in ("HH", "AA"):
            return s
        if s[:1] in "HA" and s[1:2] in "-":
            continue
    return sides(host)


def one_match(host, friend, n, out, name):
    """From the fixture's Team Select into the match and out again."""
    s = take_sides(host, friend)
    if s[:2] not in ("HH", "AA"):
        return f"{name}: the players did not take the human side together ({s})"
    starts = len([x for x in friend.lines if "joining the host's match" in x])
    for _ in range(12):  # CONTINUE (and whatever the mode shows before the match)
        if any("008CC6F0" in x for x in host.lines[-400:]) or screen(host.lines) == 0:
            break
        host.key("s", 2500)
    if not friend.wait_for(lambda: len([x for x in friend.lines if "joining the host's match" in x]) > starts, 120):
        return f"{name}: the friend did not follow the host into match {n}"
    if not host.wait_for(lambda: any(re.search(r"ms: quit at frame", x) for x in host.lines[-3000:]), 240):
        for _ in range(60):  # the intro and the set pieces wait for answers
            host.key("s", 1500)
            if any("ms: quit at frame" in x for x in host.lines[-3000:]):
                break
    if not wait_screen(friend, PARTY, 120):
        return f"{name}: after match {n} the friend is not back in its lobby"
    out.append(f"{name}: match {n} played together, the friend back in its lobby ({side_text(friend)})")
    return None


def side_text(friend):
    for x in reversed(friend.lines):
        if x.startswith("ms: party status "):
            return x[len("ms: party status ") :]
    return ""


def run_mode(br, port, name, route, out):
    host = Player(br, "host", port, env="RT_MS_QUIT_AT=10")
    friend = Player(br, "friend", port)
    try:
        for p in (host, friend):
            err = to_menus(p)
            if err:
                return f"{name}: {err}"
        code = host_room(host)
        if not code or not join_room(friend, code):
            return f"{name}: no party"
        if name == "play now":
            err = play_now(host.page, host.lines, until=TEAM_SELECT)
            if err:
                return f"{name}: {err}"
            host.key("ArrowLeft", 1500)
            friend.key("ArrowLeft", 2500)
            err = play_now(host.page, host.lines)
            if err or not friend.wait("joining the host's match", 120):
                return f"{name}: the friend did not follow the host's PLAY NOW match ({err})"
            for _ in range(40):
                host.key("s", 1500)
                if any("ms: quit at frame" in x for x in host.lines):
                    break
            if not wait_screen(friend, PARTY, 120):
                return f"{name}: after the match the friend is not back in its lobby"
            out.append(f"{name}: a match together; the friend back in its lobby ({side_text(friend)})")
            return None
        for k in UNLOCK + route:
            host.key(k, 2500)
        if not to_fixture(host):
            return f"{name}: the host's route did not reach a fixture's Team Select (screen {screen(host.lines)})"
        for n in (1, 2):
            err = one_match(host, friend, n, out, name)
            if err:
                return err
            if n == 1 and not to_fixture(host):  # the next fixture (a hub's NEXT MATCH, ...)
                return f"{name}: no next fixture after match 1 (screen {screen(host.lines)})"
        return None
    finally:
        for p in (host, friend):
            tag = name.replace(" ", "_")
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
            br = browser(pw)
            for name in names:
                err = run_mode(br, port, name, MODES.get(name, []), out)
                if err:
                    fails.append(err)
                    out.append(f"FAIL {err}")
            br.close()
    finally:
        srv.kill()
        open(os.path.join(D, "modes.log"), "w").write("\n".join(out) + "\n")
    ok = len(names) - len(fails)
    return finish(f"FAIL ({ok}/{len(names)} modes; {'; '.join(fails)})" if fails else f"ok ({ok}/{len(names)} modes)")


def finish(result):
    print(f"party_modes_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
