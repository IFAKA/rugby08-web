#!/usr/bin/env python3
"""ls.lag: a lockstep party's inputs go peer to peer, the input delay follows the measured trips, the
match runs smooth on a slow network.

Two pages (headless Chrome, muted, relay on a free port), each holding back everything it sends for
MS +- JITTER ms (?netlag=, js/peers.js: a slow, jittery network both ways). The host creates a party,
the friend joins, both play a PLAY NOW match. Measured over a minute of the match, from each game's
"netplay: pace at frame F: delay D inputs first by peer P by relay R stalls S" lines (every 600 frames):
  peer     the other player's inputs come first peer to peer (WebRTC), not by the relay (>= 90 %);
  delay    the input delay covers the measured trip (frames, vs the network's worst one-way trip);
  smooth   frames waited for an input (> 0.1 s) at most 3 per 600, and >= 55 fps in both pages.
Then the relay alone (?rtc=0 for a fresh friend): the session still joins and keeps step. One line;
details in logs/ls_lag/."""

import os
import re
import sys
import time

from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay, to_menus
from web_route import browser

sys.path.insert(0, os.path.dirname(__file__))
from ls_match_check import into_match, play  # noqa: E402
from ls_party_check import in_sync, join  # noqa: E402

D = os.path.join(ROOT, "logs", "ls_lag")
LAG, JITTER = 30, 10
STAT = re.compile(r"netplay: pace at frame (\d+): delay (\d+) inputs first by peer (\d+) by relay (\d+) stalls (\d+)")


def stats(p, after):
    return [tuple(map(int, m.groups())) for x in p.lines for m in [STAT.search(x)] if m and int(m.group(1)) > after]


def fps(players, secs):
    a = [(p.frames(), time.time()) for p in players]
    players[0].page.wait_for_timeout(int(secs * 1000))
    return [(p.frames() - n) / (time.time() - t) for p, (n, t) in zip(players, a)]


def measure(host, friend, out):
    start = int(host.last(r"ms: game state 008CC6F0 at frame (\d+)"))
    play([host, friend], 60)
    rates = fps([host, friend], 10)
    worst = (LAG + JITTER) / 16.7
    for p, r in zip((host, friend), rates):
        s = stats(p, start)
        if len(s) < 3:
            return f"{p.name}: only {len(s)} measures in the match"
        peer, rel = sum(x[2] for x in s), sum(x[3] for x in s)
        delay, stalls = max(x[1] for x in s), max(x[4] for x in s)
        out.append(
            f"{p.name}: {100 * peer / max(1, peer + rel):.0f} % of inputs first by peer, delay {delay} frames "
            f"(worst trip {worst:.1f}), at most {stalls} stalls per 600 frames, {r:.0f} fps"
        )
        if peer < 0.9 * (peer + rel):
            return f"{p.name}: the inputs came by the relay ({peer} by peer, {rel} by relay)"
        if delay < worst or delay > worst + 4:
            return f"{p.name}: input delay {delay} frames for a {worst:.1f}-frame trip"
        if stalls > 3 or r < 55:
            return f"{p.name}: not smooth ({stalls} stalls per 600 frames, {r:.0f} fps)"
    return None


def relay_only(br, port, host, out):
    """A friend whose page has no peer channels (?rtc=0): everything by the relay, as before."""
    f = Player(br, "relayfriend", port, query="&rtc=0")
    err = to_menus(f)
    r = err or join(f, host.page.evaluate("Module.rugbyRoom || ''"))
    out.append(f"relay only: {r}")
    if not r.startswith("caught up"):
        return f, f"a relay-only friend did not join: {r}"
    play([host, f], 20)
    return f, None if in_sync([host, f], out, "relay only") else "the relay-only friend is not in sync"


def run(br, port, out):
    q = f"&netlag={LAG},{JITTER}"
    host, friend = Player(br, "host", port, query=q), Player(br, "friend", port, query=q)
    players = [host, friend]
    try:
        err = into_match(host, friend, out)
        if err:
            return err
        if not all(p.wait("ms: peer open with player", 30) for p in players):
            return "no peer-to-peer channel opened"
        err = measure(host, friend, out)
        if err:
            return err
        friend.page.close()
        f, err = relay_only(br, port, host, out)
        players.append(f)
        return err
    finally:
        for p in players:
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
    print(f"ls_lag_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
