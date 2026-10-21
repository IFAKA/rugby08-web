#!/usr/bin/env python3
"""ls.decisions: a set piece's options are the deciding team's; every other screen says who is choosing.

Two pages (headless Chrome, muted, relay on a free port), the host's keyboard home, the friend's pad away,
a PLAY NOW match (runtime/core/ls_roles.c others_choice, match_state.c ms_setpiece_team). At each quick or
penalty options menu (0x58, 0x5F) only the friend presses Cross, then (if the menu stays) only the host:
the one whose press closes it is the one the game lets choose. While it is up:
  the other's screen   "ms: party role PLAYER N IS CHOOSING" with N the chooser's player number;
  the chooser's screen no such cover (its own menu);
and the match's state hashes equal in both games. Passes with at least 4 menus judged, all right.
One line; details in logs/ls_decisions/."""

import os
import sys
import time

from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay
from web_route import browser, screen

sys.path.insert(0, os.path.dirname(__file__))
from ls_match_check import into_match, phases  # noqa: E402
from ls_party_check import in_sync  # noqa: E402

D = os.path.join(ROOT, "logs", "ls_decisions")
MENUS = (0x58, 0x5F)


def opened(p, sid):
    """The index of this page's line saying the menu opened (its banner comes at that very frame)."""
    tag = f"id {sid:04X})"
    i = max((i for i, x in enumerate(p.lines) if "ms: fe screen" in x and tag in x), default=len(p.lines))
    while i > 0 and "ms: fe screen" in p.lines[i - 1]:  # (the frame's other copy of that line)
        i -= 1
    return max(0, i - 8)  # (the banner is traced at that frame's Present, just before the screen's line)


def roles(p, n0):
    return [x for x in p.lines[n0:] if "ms: party role PLAYER" in x and "CHOOSING" in x]


def judge(host, friend, sid, out):
    """Who closes the menu, and whether each screen showed the right thing while it was up."""
    host.page.wait_for_timeout(1500)
    n = {p.name: opened(p, sid) for p in (host, friend)}
    by = None
    for who in (friend, host):
        for _ in range(3):
            who.key("s", 700)
            if screen(host.lines) != sid:
                by = who
                break
        if by:
            break
    if not by:
        out.append(f"menu {sid:02X}: nobody's press closed it")
        return None
    other = host if by is friend else friend
    want = f"PLAYER {2 if by is friend else 1} IS CHOOSING"
    seen_other = any(want in x for x in roles(other, n[other.name]))
    seen_by = roles(by, n[by.name])
    ok = seen_other and not seen_by
    out.append(
        f"menu {sid:02X}: {by.name} chose; {other.name} saw '{want}' {seen_other}, {by.name} covered {bool(seen_by)}"
    )
    return ok


def run(br, port, out):
    host, friend = Player(br, "host", port), Player(br, "friend", port)
    try:
        err = into_match(host, friend, out)
        if err:
            return err
        verdicts, t0 = [], time.time()
        while time.time() - t0 < 900 and len([v for v in verdicts if v is not None]) < 6:
            sid = screen(host.lines)
            if sid in MENUS:
                verdicts.append(judge(host, friend, sid, out))
                host.page.wait_for_timeout(2000)
                continue
            for p in (host, friend):  # play on, Cross now and then (not in a scrum's formation)
                if phases(host)[-1:] == ["Formation"]:
                    p.page.wait_for_timeout(700)
                else:
                    p.key("s", 700)
        judged = [v for v in verdicts if v is not None]
        if len(judged) < 4:
            return f"only {len(judged)} menus judged"
        if not all(judged):
            return "a screen showed the wrong thing while a set piece's options were up"
        return None if in_sync([host, friend], out, "match") else "the two games are not in sync"
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
    print(f"ls_decisions_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
