#!/usr/bin/env python3
"""ls.party: the ONLINE screens drive the lockstep party (runtime/core/ls_party.c, platform/web/shell.html).

Three pages (headless Chrome, muted, relay on a free port), each opened as players open the game. The
host goes ONLINE > CREATE PARTY: its screen shows its session's code. Each friend, in its own game,
goes ONLINE > JOIN PARTY and types the code (keyboard, Enter): its page reloads into the host's
session, catches up (time measured) and from then on shows the host's game: the same screen, the
roster on the PARTY screen and the badge count the players; all games print the same state hashes.
The shared menus are the host's: a friend's screen says the host is choosing; the first friend leaves
from its own prompt (Esc, then S) as in online sports games: its game plays on (no restart), at its main
menu, in a party of its own (a new code), "YOU LEFT THE PARTY"; the others see it leave and stay in sync.
A newcomer joins the leaver's own party and is in sync with it (its log is the leaver's game); then the
leaver joins the host's party again and plays along (not removed by its slot's earlier leave). One line;
details in logs/ls_party/."""

import os
import re
import sys
import time

from playwright.sync_api import sync_playwright
from web_party import MAIN, ONLINE, PARTY, ROOT, Player, relay, to_menus, wait_screen
from web_route import browser, screen

D = os.path.join(ROOT, "logs", "ls_party")
HASH = re.compile(r"netplay: frame (\d+) hash (\S+)")
JOIN = 7102


def hashes(lines):
    return {int(m.group(1)): m.group(2) for x in lines for m in [HASH.search(x)] if m}


def in_sync(players, out, what):
    hs = [hashes(p.lines) for p in players]
    common = sorted(set.intersection(*(set(h) for h in hs)))
    common = [f for f in common if f >= max(min(h) for h in hs)]
    bad = [f for f in common if len({h[f] for h in hs}) > 1]
    out.append(f"{what}: {len(common)} hashes compared, {len(bad)} different")
    return len(common) >= 2 and not bad


def create(host):
    for k in ("ArrowDown", "s"):
        host.key(k)
    if not wait_screen(host, ONLINE, 30):
        return None
    host.key("s", 3000)  # CREATE PARTY
    if not wait_screen(host, PARTY, 30):
        return None
    return host.page.evaluate("Module.rugbyRoom || ''")


def to_join(friend):
    for k in ("ArrowDown", "s"):
        friend.key(k)
    if not wait_screen(friend, ONLINE, 30):
        return "no ONLINE screen"
    friend.key("ArrowDown")
    friend.key("s", 3000)  # JOIN PARTY
    return None if wait_screen(friend, JOIN, 30) else "no JOIN screen"


def type_code(friend, code):
    for c in code:
        friend.key(f"Key{c}" if c.isalpha() else f"Digit{c}", 400)


def wrong_code(friend):
    """A code nobody has: the game says so on its JOIN screen (no page message) and stays."""
    err = to_join(friend)
    if err:
        return err
    type_code(friend, "ZZZZZZ")
    friend.key("Enter", 1000)
    if not friend.wait("party toast NO PARTY WITH THAT CODE", 30):
        return "a wrong code was not told in the game"
    for _ in range(6):
        friend.key("Backspace", 300)
    return None


def join(friend, code, on_join=False):
    if not on_join:
        err = to_join(friend)
        if err:
            return err
    for c in code:
        friend.key(f"Key{c}" if c.isalpha() else f"Digit{c}", 400)
    n = len(friend.lines)
    t0 = time.time()
    friend.key("Enter", 1000)
    if not friend.wait_for(lambda: any("caught up at frame" in x for x in friend.lines[n:]), 600):
        return "it did not catch up with the host's session"
    return f"caught up in {time.time() - t0:.0f} s"


def leave(host, friends, code, out):
    """The first friend leaves: its game plays on (no new session) at its main menu, in its own room."""
    f1 = friends[0]
    if not f1.wait("THE HOST IS CHOOSING", 30):
        return None, "the first friend was not told the host is choosing"
    f1.key("Escape", 1500)  # a friend leaves from its own prompt (the shared menus are the host's)
    if not f1.wait("LEAVE THE PARTY?", 10):
        return None, "Esc did not ask the first friend whether to leave"
    n = len(f1.lines)
    f1.key("s", 3000)
    if not f1.wait_for(lambda: any("left the party: playing on in room" in x for x in f1.lines[n:]), 60):
        return None, "the first friend's LEAVE PARTY did not keep its game in a room of its own"
    own = f1.page.evaluate("Module.rugbyRoom || ''")
    if not wait_screen(f1, MAIN, 30) or not f1.wait("party toast YOU LEFT THE PARTY", 10):
        return None, "the first friend is not at its main menu with YOU LEFT THE PARTY"
    if any("session started" in x for x in f1.lines[n:]) or own in ("", code):
        return None, f"the first friend's game restarted or kept the party's code ({own})"
    out.append(f"friend1: left, plays on in room {own} at its main menu")
    if not host.wait("party toast PLAYER 2 LEFT", 60):
        return None, "the host was not told player 2 left"
    host.page.wait_for_timeout(15000)
    if not in_sync([host, friends[1]], out, "after the leave"):
        return None, "the host and the second friend are not in sync after the leave"
    return own, None


def newcomer(f3, f1, own, out):
    """A newcomer in the leaver's own room replays its log: the same game as the leaver's."""
    err = to_menus(f3)
    r = err or join(f3, own)
    out.append(f"friend3 into {own}: {r}")
    if not r.startswith("caught up"):
        return f"friend3: {r}"
    f3.page.wait_for_timeout(15000)
    if not in_sync([f1, f3], out, "the leaver and a newcomer in its room"):
        return "a newcomer in the leaver's room is not in sync with it"
    n = len(f1.lines)
    f3.page.close()  # (the leaver alone again: its ONLINE screen offers JOIN PARTY)
    if not f1.wait_for(lambda: any("party toast PLAYER" in x and "LEFT" in x for x in f1.lines[n:]), 60):
        return "the leaver was not told the newcomer left"
    return None


def rejoin(host, friends, code, out):
    """The leaver joins the host's party again: it catches up and plays along (its slot's earlier
    player, itself, left the log: not this player's own leave)."""
    f1 = friends[0]
    if not wait_screen(f1, MAIN, 30):
        return "the leaver is not at its main menu"
    r = join(f1, code)
    out.append(f"friend1 back in {code}: {r}")
    if not r.startswith("caught up"):
        return f"friend1 back: {r}"
    n = len(f1.lines)
    host.page.wait_for_timeout(20000)
    if any("removed from the session" in x or "connection lost" in x for x in f1.lines[n:]):
        return "the friend who came back was removed from the party"
    if not in_sync([host] + friends, out, "after coming back"):
        return "the three games are not in sync after the friend came back"
    return None


def run(br, port, out):
    host = Player(br, "host", port)
    friends = [Player(br, f"friend{i}", port) for i in (1, 2)]
    players = [host] + friends
    try:
        for p in players:
            err = to_menus(p)
            if err:
                return err
        code = create(host)
        if not code:
            return "CREATE PARTY did not show the party screen with a code"
        out.append(f"host: party {code}")
        err = wrong_code(friends[1])
        if err:
            return f"friend2: {err}"
        out.append("friend2: a wrong code is told in the game")
        for f in friends:
            r = join(f, code, on_join=f is friends[1])
            out.append(f"{f.name}: {r}")
            if not r.startswith("caught up"):
                return f"{f.name}: {r}"
            host.page.wait_for_timeout(5000)
            if screen(f.lines) != screen(host.lines):
                return f"{f.name} shows screen {screen(f.lines)}, the host {screen(host.lines)}"
        host.page.wait_for_timeout(15000)
        if not host.wait("party toast PLAYER 3 JOINED", 30) or not host.wait("party toast PLAYER 2 JOINED", 1):
            return "the host was not told in the game that players 2 and 3 joined"
        if not in_sync(players, out, "three players"):
            return "the three games are not in sync"
        own, err = leave(host, friends, code, out)
        if err:
            return err
        f3 = Player(br, "friend3", port)
        players.append(f3)
        err = newcomer(f3, friends[0], own, out)
        if err:
            return err
        return rejoin(host, friends, code, out)
    finally:
        for p in players:
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
    print(f"ls_party_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
