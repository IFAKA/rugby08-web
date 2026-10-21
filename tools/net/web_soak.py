#!/usr/bin/env python3
"""pt.freeze: the browser game keeps running for a long time, also in a background tab.

Two pages of the web build (headless Chrome, muted; the relay serves the build on a free port) start
the game, click into it (that starts their AudioContext: the old test "freeze") and form a party (the
in-game Online screens); the host plays PLAY NOW into a match and the friend follows it. Then the
host's tab goes to the background for HIDDEN minutes: headless Chrome never hides a page, so the page
gets what Chrome gives a hidden tab (document.hidden, a visibilitychange, a blur, no animation frames,
main-thread timers clamped to once a second), and back. Every 10 s both pages are sampled.

  tools/net/web_soak.py [--minutes M] [--hidden H]      (default 10 minutes, 3 of them hidden)

Pass: no page dies (FATAL) or restores its sound device (DirectSound cursor stalls); every sample
both pages advanced (>= 5 fps; a throttled background tab runs at 1); the hidden host keeps >= 70 % of
its visible frame rate, its match time advances and the friend receives its frames (in total over the
hidden time: cutscenes stop the match clock and are not sent). One line; details in logs/web_soak/."""

import os
import sys
import time

from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, host_room, join_room, relay, to_menus
from web_route import browser, play_now

D = os.path.join(ROOT, "logs", "web_soak")
HIDE = """() => {
  const o = window.rtShown = { raf: window.requestAnimationFrame, st: window.setTimeout, si: window.setInterval };
  Object.defineProperty(document, 'hidden', { configurable: true, get: () => true });
  Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'hidden' });
  window.requestAnimationFrame = () => 0;
  window.setTimeout = (f, ms, ...a) => o.st(f, Math.max(ms | 0, 1000), ...a);
  window.setInterval = (f, ms, ...a) => o.si(f, Math.max(ms | 0, 1000), ...a);
  document.dispatchEvent(new Event('visibilitychange'));
  window.dispatchEvent(new FocusEvent('blur'));
}"""
SHOW = """() => {
  const o = window.rtShown;
  delete document.hidden;
  delete document.visibilityState;
  window.requestAnimationFrame = o.raf; window.setTimeout = o.st; window.setInterval = o.si;
  document.dispatchEvent(new Event('visibilitychange'));
  window.dispatchEvent(new FocusEvent('focus'));
}"""


def party(host, friend):
    for p in (host, friend):
        err = to_menus(p)
        if err:
            return err, None
    code = host_room(host)
    if not code:
        return "no room", None
    if not join_room(friend, code):
        return f"the friend did not join {code}", code
    err = play_now(host.page, host.lines)
    if err:
        return f"the host reached no match: {err}", code
    for _ in range(4):  # the intro
        host.key("s", 4000)
    if not friend.wait("joining the host's match", 120):
        return "the friend did not follow the host into its match", code
    return None, code


def soak(host, friend, minutes, hidden, log):
    t0, samples = time.time(), []
    hide_at, show_at = 60.0, 60.0 + hidden * 60
    state = "shown"
    prev = (host.frames(), friend.frames(), host.last(r"ms: pos f \d+ t ([\d.]+)"), friend.last(r"ms: net rx (\d+)"))
    while time.time() - t0 < minutes * 60:
        el = time.time() - t0
        if state == "shown" and el >= hide_at and el < show_at:
            host.page.evaluate(HIDE)
            state = "hidden"
        elif state == "hidden" and el >= show_at:
            host.page.evaluate(SHOW)
            state = "back"
        ts = time.time()
        host.page.wait_for_timeout(10000)
        dt = time.time() - ts
        cur = (host.frames(), friend.frames(), host.last(r"ms: pos f \d+ t ([\d.]+)"), friend.last(r"ms: net rx (\d+)"))
        s = {
            "t": round(el),
            "state": "hidden" if state == "hidden" else "shown",
            "dt": dt,
            "host_fps": (cur[0] - prev[0]) / dt,
            "friend_fps": (cur[1] - prev[1]) / dt,
            "match_t": cur[2] - prev[2],
            "rx": cur[3] - prev[3],
        }
        samples.append(s)
        log.write(" ".join(f"{k}={v:.1f}" if isinstance(v, float) else f"{k}={v}" for k, v in s.items()) + "\n")
        log.flush()
        prev = cur
    return samples


def judge(host, friend, samples):
    for p in (host, friend):
        if p.count("FATAL"):
            return f"{p.name} died (FATAL)"
        if p.count("not advancing, restoring"):
            return f"{p.name} restored its sound device {p.count('not advancing, restoring')} times"
    slow = [s for s in samples if min(s["host_fps"], s["friend_fps"]) < 5]
    if slow:
        s = slow[0]
        return f"a page stalled at {s['t']} s ({s['state']}): host {s['host_fps']:.0f} fps, friend {s['friend_fps']:.0f} fps"
    hid = [s for s in samples if s["state"] == "hidden"][1:]  # (the first one straddles the switch)
    shown = [s for s in samples if s["state"] == "shown"]
    if not hid or not shown:
        return "no hidden or no shown samples"
    fps_h = sum(s["host_fps"] for s in hid) / len(hid)
    fps_s = sum(s["host_fps"] for s in shown) / len(shown)
    if fps_h < 0.7 * fps_s:
        return f"the hidden host slowed down: {fps_h:.0f} fps vs {fps_s:.0f} shown"
    mt, rx = sum(s["match_t"] for s in hid), sum(s["rx"] for s in hid)
    if mt <= 0 or rx <= 0:
        return f"the hidden host's match stopped (match time +{mt:.1f} s, friend received {rx:.0f} frames)"
    return None, (
        f"host {fps_s:.0f} fps shown, {fps_h:.0f} fps hidden over {len(hid) * 10} s "
        f"(match time +{mt:.0f} s, friend received {rx:.0f} host frames)"
    )


def main(argv):
    minutes = float(argv[argv.index("--minutes") + 1]) if "--minutes" in argv else 10
    hidden = float(argv[argv.index("--hidden") + 1]) if "--hidden" in argv else 3
    os.makedirs(D, exist_ok=True)
    srv, port = relay(D)
    if not srv:
        return finish("FAIL (relay did not build or start)")
    result = "FAIL (setup)"
    try:
        with sync_playwright() as pw:
            br = browser(pw)
            host, friend = Player(br, "host", port), Player(br, "friend", port)
            try:
                err, code = party(host, friend)
                if err:
                    result = f"FAIL ({err})"
                else:
                    with open(os.path.join(D, "samples.log"), "w") as log:
                        samples = soak(host, friend, minutes, hidden, log)
                    v = judge(host, friend, samples)
                    result = f"FAIL ({v})" if isinstance(v, str) else f"ok (party {code}, {minutes:.0f} min: {v[1]})"
            finally:
                for p in (host, friend):
                    open(os.path.join(D, p.name + ".log"), "w").write("\n".join(p.lines))
                br.close()
    finally:
        srv.kill()
    return finish(result)


def finish(result):
    print(f"web_soak: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
