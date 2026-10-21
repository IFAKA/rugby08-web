#!/usr/bin/env python3
"""pt.camera: each player's own camera in a party's match (runtime/core/match_camera.c).

Two pages (host, friend; headless Chrome, muted, relay on a free port) play a party match. The friend's
camera style is set differently from the host's (RT_TEST_CAMERA=2 Broadcast, a test switch writing the
option its menus would; the host keeps Side). Each player sees the match as if playing alone (the user,
2026-10-13): in open play each game shows its own style's camera, following the play; at set pieces and
events the friend shows the host's camera (its own game, whose match logic does not run, has nothing to
direct one). Both trace their view ("ms: cam f F NAME eye .. target .. ball .."; the host every frame,
the friend every 15). Pass, over a minute of the match: the friend shows its own style (Broadcast or Zoom
Broadcast, never Side), its own views follow the ball (target vs ball along the pitch: correlation >=
0.5), some of its views are the host's set-piece views, and the personal settings are listed
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


def views(lines):
    out = []
    for x in lines:
        m = CAM.match(x)
        if m and m.group(3) != "1":  # (before the first view: the default eye 1 1000 1)
            out.append((m.group(2), tuple(float(m.group(k)) for k in range(3, 9)), float(m.group(10)) * 100))
    return out


def corr(a, b):
    n = len(a)
    if n < 10:
        return 0.0
    ma, mb = sum(a) / n, sum(b) / n
    va, vb = sum((x - ma) ** 2 for x in a), sum((y - mb) ** 2 for y in b)
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (va * vb) ** 0.5 if va and vb else 0.0


def judge(host, friend, out):
    h, f = views(host.lines), views(friend.lines)
    if len(h) < 600 or len(f) < 60:
        return f"too few camera samples (host {len(h)}, friend {len(f)})"
    hv = {v: name for name, v, _ in h}
    shared = [hv[v] for _, v, _ in f if v in hv]
    own = [(n, v, b) for n, v, b in f if v not in hv]
    names = sorted({n for n, _, _ in own})
    out.append(f"friend: {len(own)} own views ({names}), {len(shared)} the host's ({sorted(set(shared))})")
    if any("Side" in n for n in names) or not any("Broadcast" in n for n in names):
        return f"the friend does not show its own style in open play ({names})"
    if not shared or any(n in ("Side", "Zoom Side") for n in shared):
        return f"the friend's shared views are not the host's set-piece cameras ({sorted(set(shared))})"
    r = corr([v[5] for _, v, _ in own], [b for _, _, b in own])  # target z vs ball z (cm)
    out.append(f"own view target/ball correlation {r:.2f}")
    if r < 0.5:
        return f"the friend's own camera does not follow the ball (correlation {r:.2f})"
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
            host = Player(br, "host", port, env="RT_MS_CAM=1")
            friend = Player(br, "friend", port, env="RT_MS_CAM=15,RT_TEST_CAMERA=2")
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
