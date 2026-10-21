#!/usr/bin/env python3
"""pt.pause_leave: pausing and leaving a party's match (runtime/core/net_ha_flow.c, party_leave.c).

Pages (headless Chrome, muted, relay on a free port): a host and two friends play a party match (host
and the second friend at home, the first friend away). Then:
  pause    the first friend's Esc pauses everyone (the host's game pauses, the second friend's menu opens);
           the second friend moves in its own menu (the host ignores friends' pads while paused); the
           first friend's RESUME resumes everyone
  leave    the second friend closes its page: everyone is told, its controller goes, the teams are rebuilt
           (the AI takes its player), the host's match goes on without a reassign screen
  join     a third friend joins mid-match: it follows the host into the match and watches (no side)
  end      the host closes its page: the party ends, each friend leaves the match for its own main menu
One line; details in logs/party_pause_leave/."""

import os
import re
import sys

from playwright.sync_api import sync_playwright
from web_party import MAIN, ROOT, Player, host_room, join_room, relay, to_menus, wait_screen
from web_route import TEAM_SELECT, browser, play_now, screen

D = os.path.join(ROOT, "logs", "party_pause_leave")
PAUSED = 0x71
HUD = 0x79
REASSIGN = (0x25E, 0x98, 0x99)


def match_time(p):
    for x in reversed(p.lines):
        m = re.match(r"ms: pos f \d+ t ([\d.]+)", x)
        if m:
            return float(m.group(1))
    return 0.0


def play(players, secs):
    """The match's prompts wait for its humans (each side's set pieces): every player answers with Cross now
    and then (a friend's goes to the host as its pad)."""
    players = players if isinstance(players, list) else [players]
    for _ in range(int(secs / 1.5)):
        for p in players:
            p.key("s", 1500 // len(players))


def into_match(host, f1, f2):
    for p in (host, f1, f2):
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
            return f"{f.name} did not follow into Team Select"
    for p, k in ((host, "ArrowLeft"), (f1, "ArrowRight"), (f2, "ArrowLeft")):
        p.key(k, 2000)
    err = play_now(host.page, host.lines)
    if err:
        return f"host: {err}"
    for _ in range(4):  # the intro
        host.key("s", 4000)
    for f in (f1, f2):
        if not f.wait("joining the host's match", 120):
            return f"{f.name} did not follow the host into its match"
    play([host, f1, f2], 15)
    return code


def pause(host, f1, f2, out):
    for _ in range(30):  # in open play (the game does not pause inside a set piece's menus)
        if screen(host.lines) == HUD:
            break
        play([host, f1, f2], 1.5)
    f1.key("Escape", 3000)
    if not host.wait("a friend pauses", 20) or not host.wait_for(lambda: screen(host.lines) == PAUSED, 20):
        return "the first friend's Esc did not pause the host"
    if not f2.wait_for(lambda: screen(f2.lines) == PAUSED, 20):
        return "the second friend's pause menu did not open"
    f2.key("ArrowDown", 1500)  # its own menu (the host ignores friends' pads while paused)
    t = match_time(host)
    host.page.wait_for_timeout(3000)
    if match_time(host) != t:
        return "the host's match went on while paused"
    f1.key("s", 3000)  # RESUME (highlighted first)
    if not host.wait("a friend resumes", 20) or not host.wait_for(lambda: screen(host.lines) != PAUSED, 20):
        return "the first friend's RESUME did not resume the host"
    if not f2.wait_for(lambda: screen(f2.lines) != PAUSED, 20):
        return "the second friend's menu stayed open"
    out.append(f"pause: friend 1 paused and resumed everyone; the match held at {t:.1f}")
    return None


def leave(host, f1, f2, out):
    f2.page.close()
    if not host.wait("ms: party left player 3", 30) or not host.wait("ms: party toast PLAYER 3 LEFT", 10):
        return "the host was not told player 3 left"
    t = match_time(host)
    play([host, f1], 12)
    if match_time(host) <= t or screen(host.lines) in REASSIGN + (PAUSED,):
        return f"the host's match stopped after the leave (screen {screen(host.lines)}, match time {t:.1f})"
    ctl = [x for x in host.lines if x.startswith("ms: controllers")][-1]
    if re.search(r"P2:", ctl):
        return f"the leaver's controller is still on a side: {ctl}"
    out.append(f"leave: player 3 left mid-match, the match went on ({t:.1f} -> {match_time(host):.1f}); {ctl}")
    return None


def late_join(br, port, code, host, f1, out):
    f3 = Player(br, "friend3", port)
    if to_menus(f3) or not join_room(f3, code):
        return f3, "a friend could not join during the match"
    play([host, f1], 15)  # (the match goes on: its set pieces wait for its humans)
    if not f3.wait("joining the host's match", 120) or not f3.wait("ms: net rx", 60):
        return f3, "the late friend did not follow the host into its match"
    host.page.wait_for_timeout(3000)
    ctl = [x for x in host.lines if x.startswith("ms: controllers")][-1]
    out.append(f"join: a friend joined mid-match and watches; host {ctl}")
    return f3, None


def end(host, friends, out):
    host.page.close()
    for f in friends:
        if not f.wait("ms: party ended", 30) or not wait_screen(f, MAIN, 60):
            return f"{f.name} did not leave for its main menu when the host left"
    out.append("end: the host left; every friend is back in its own main menu")
    return None


def main():
    os.makedirs(D, exist_ok=True)
    srv, port = relay(D)
    if not srv:
        return finish("FAIL (relay did not build or start)")
    out, err, players = [], None, []
    try:
        with sync_playwright() as pw:
            br = browser(pw)
            host, f1, f2 = (Player(br, n, port) for n in ("host", "friend1", "friend2"))
            players = [host, f1, f2]
            try:
                code = into_match(host, f1, f2)
                err = code if code and " " in code else None
                err = err or pause(host, f1, f2, out) or leave(host, f1, f2, out)
                if not err:
                    f3, err = late_join(br, port, code, host, f1, out)
                    players.append(f3)
                err = err or end(host, [f1, players[-1]], out)
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
        else "ok (pause and resume for everyone from a friend; a leaver's "
        "player taken by the AI, the match goes on; a mid-match joiner watches; the host leaving ends it)"
    )


def finish(result):
    print(f"party_pause_leave_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
