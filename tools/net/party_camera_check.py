#!/usr/bin/env python3
"""pt.camera: each player's own camera in a party's match (runtime/core/match_camera.c).

Two pages (host, friend; headless Chrome, muted, relay on a free port) play a party match. The friend's
camera style is set differently from the host's (RT_TEST_CAMERA=2 Broadcast, a test switch writing the
option its menus would; the host keeps its own, Side); both trace their camera every 30 frames ("ms: cam
f F NAME eye .. target .. ball .."). Pass, over a minute of the match: each game shows its own style's
cameras (the friend Broadcast / Zoom Broadcast, the host Side / Zoom Side), the friend's cameras switch on
the host's match events (forwarded 'C': it shows event cameras such as Kickoff or Zoom), its view follows
the ball (target vs ball along the pitch: correlation >= 0.5; a camera that does not follow scores ~0), and the personal settings are listed
(docs/PARTY_RESEARCH.md "Personal settings"). One line; details in logs/party_camera/."""

import os
import re
import sys

import web_soak
from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay
from web_route import browser

D = os.path.join(ROOT, "logs", "party_camera")
CAM = re.compile(
    r"ms: cam f (\d+) ([A-Za-z0-9 ]+?) eye (\S+) (\S+) (\S+) target (\S+) (\S+) (\S+) fov \S+ ball (\S+) \S+ (\S+)"
)


def cams(lines):
    seen, out = set(), []
    for x in lines:
        m = CAM.match(x)
        if m and x not in seen:
            seen.add(x)
            out.append((m.group(2), float(m.group(8)), float(m.group(10)) * 100))  # name, target z, ball z (cm)
    return out


def corr(a, b):
    n = len(a)
    if n < 10:
        return 0.0
    ma, mb = sum(a) / n, sum(b) / n
    va, vb = sum((x - ma) ** 2 for x in a), sum((y - mb) ** 2 for y in b)
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (va * vb) ** 0.5 if va and vb else 0.0


def judge(host, friend, out):
    h, f = cams(host.lines), cams(friend.lines)
    if len(h) < 60 or len(f) < 60:
        return f"too few camera samples (host {len(h)}, friend {len(f)})"
    hn = {n for n, *_ in h}
    fn = {n for n, *_ in f}
    out.append(f"host cameras {sorted(hn)}, friend cameras {sorted(fn)}")
    own = sum(1 for n, *_ in f if "Broadcast" in n) / len(f)
    if own < 0.7 or any("Side" in n for n in fn):
        return f"the friend does not show its own style (Broadcast {own:.0%}, cameras {sorted(fn)})"
    if not any("Side" in n for n in hn) or any("Broadcast" in n for n in hn):
        return f"the host does not show its own style (cameras {sorted(hn)})"
    events = len({x for x in friend.lines if "ms: cam host event" in x})
    if not events or not (fn - {"Broadcast"}):
        return f"the friend's cameras did not follow the host's match events ({events} events, {sorted(fn)})"
    play = [(t, b) for n, t, b in f if n == "Broadcast"]
    r = corr([t for t, _ in play], [b for _, b in play])
    hplay = [(t, b) for n, t, b in h if n == "Side"]
    rh = corr([t for t, _ in hplay], [b for _, b in hplay])
    out.append(f"friend: {events} host events, {own:.0%} own style, target/ball correlation {r:.2f} (host {rh:.2f})")
    if r < 0.5:  # a camera that does not follow the play scores ~0 (the friend's before pt.camera's events)
        return f"the friend's camera does not follow the ball (correlation {r:.2f}, host {rh:.2f})"
    doc = open(os.path.join(ROOT, "docs", "PARTY_RESEARCH.md")).read()
    if "## Personal settings" not in doc:
        return "the personal settings are not listed"
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
            host = Player(br, "host", port, env="RT_MS_CAM=30")
            friend = Player(br, "friend", port, env="RT_MS_CAM=30,RT_TEST_CAMERA=2")
            try:
                err, _ = web_soak.party(host, friend)
                if not err:
                    host.page.wait_for_timeout(60000)
                    err = judge(host, friend, out)
            finally:
                for p in (host, friend):
                    open(os.path.join(D, p.name + ".log"), "w").write("\n".join(p.lines))
                br.close()
    finally:
        srv.kill()
        open(os.path.join(D, "steps.log"), "w").write("\n".join(out) + "\n")
    return finish(f"FAIL ({err})" if err else f"ok ({'; '.join(out)})")


def finish(result):
    print(f"party_camera_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
