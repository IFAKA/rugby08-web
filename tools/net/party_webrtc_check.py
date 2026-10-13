#!/usr/bin/env python3
"""pt.webrtc: a party's traffic goes peer to peer over WebRTC data channels (platform/web/js/rtc.js), the
server only signals and relays as a fallback; latency measured.

Two sessions of two pages each (host, friend; headless Chrome, muted; relay on a free port), each a
party whose host plays PLAY NOW and whose friend follows it into the match, sampled for SECS of play:
  relay   ?rtc=0: everything through the server (the fallback path)
  webrtc  data channels; the host's sender drops 0.3 % of its frames (?rtcloss=0.003, a test switch:
          frames go over a reliable channel, so only a path change loses one) so the friend's recovery
          runs: a frame missing for 30 later ones ends its chain until a key it asks for
The latency of a frame is the friend's receive time minus the host's send time (the trace's "net tx" /
"net rx" lines: one clock in both pages, performance.timeOrigin + now).

Pass: in both sessions the friend follows the host into its match and shows its frames; with WebRTC
both ends open the channels, >= 95 % of the host's game messages in the match go over them, the
friend asks for keys after losses and keeps receiving (>= 85 % of the host's frames), and its latency
p95 is no worse than the relay's (+2 ms). One line; details in logs/party_webrtc/.

  tools/net/party_webrtc_check.py [--secs S]      (default 60)"""

import os
import re
import sys

from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, host_room, join_room, relay, to_menus
from web_route import browser, play_now

D = os.path.join(ROOT, "logs", "party_webrtc")
STATS = "Module.rtc ? Object.assign({}, Module.rtc.stats) : {}"


def times(lines, what):
    """frame -> ns of the first "ms: net tx|rx FRAME ..." line."""
    out = {}
    for x in lines:
        m = re.match(rf"ms: net {what} (\d+) \w+ ns (\d+)", x)
        if m and int(m.group(1)) not in out:
            out[int(m.group(1))] = int(m.group(2))
    return out


def pct(v, q):
    v = sorted(v)
    return v[min(len(v) - 1, int(q * len(v)))] if v else float("nan")


def session(br, port, name, query, secs, out):
    host, friend = Player(br, f"{name}_host", port, query), Player(br, f"{name}_friend", port, query)
    try:
        for p in (host, friend):
            err = to_menus(p)
            if err:
                return err, None
        code = host_room(host)
        if not code or not join_room(friend, code):
            return "no party", None
        if "rtc=0" not in query:
            for p in (host, friend):
                if not p.wait("ms: rtc open with player", 30):
                    return f"{p.name}: no data channels", None
        err = play_now(host.page, host.lines)
        if err:
            return f"host: {err}", None
        for _ in range(4):  # the intro
            host.key("s", 4000)
        if not friend.wait("joining the host's match", 120):
            return "the friend did not follow the host", None
        s0, n0 = host.page.evaluate(STATS), len(host.lines)
        friend.page.wait_for_timeout(int(secs * 1000))
        s1 = host.page.evaluate(STATS)
        tx, rx = times(host.lines[n0:], "tx"), times(friend.lines, "rx")
        lat = [(rx[f] - t) / 1e6 for f, t in tx.items() if f in rx]
        r = {
            "tx": len(tx),
            "rx": sum(1 for f in tx if f in rx),
            "p50": pct(lat, 0.5),
            "p95": pct(lat, 0.95),
            "dc": s1.get("dc", 0) - s0.get("dc", 0),
            "ws": s1.get("ws", 0) - s0.get("ws", 0),
            "dropped": s1.get("dropped", 0) - s0.get("dropped", 0),
            "asked": len({x for x in friend.lines if "asking for a key" in x}),  # (each trace line comes twice)
        }
        out.append(f"{name}: {r}")
        return None, r
    finally:
        for p in (host, friend):
            open(os.path.join(D, p.name + ".log"), "w").write("\n".join(p.lines))
            p.page.context.close()


def judge(rel, rtc):
    if rel["rx"] < 0.95 * rel["tx"] or rel["tx"] < 100:
        return f"relay: the friend received {rel['rx']} of {rel['tx']} host frames"
    if rtc["dc"] < 0.95 * (rtc["dc"] + rtc["ws"]):
        return f"webrtc: only {rtc['dc']} of {rtc['dc'] + rtc['ws']} game messages went over the data channels"
    if rtc["tx"] < 100 or rtc["rx"] < 0.85 * rtc["tx"]:
        return f"webrtc: the friend received {rtc['rx']} of {rtc['tx']} host frames"
    if rtc["dropped"] and not rtc["asked"]:
        return f"webrtc: {rtc['dropped']} frames dropped but the friend never asked for a key"
    if not rtc["p95"] <= rel["p95"] + 2:
        return f"webrtc latency p95 {rtc['p95']:.1f} ms vs relay {rel['p95']:.1f} ms"
    return None


def main(argv):
    secs = float(argv[argv.index("--secs") + 1]) if "--secs" in argv else 60
    os.makedirs(D, exist_ok=True)
    srv, port = relay(D)
    if not srv:
        return finish("FAIL (relay did not build or start)")
    out, result = [], "FAIL (setup)"
    try:
        with sync_playwright() as pw:
            br = browser(pw)
            err, rel = session(br, port, "relay", "&rtc=0", secs, out)
            if not err:
                err, rtc = session(br, port, "webrtc", "&rtcloss=0.003", secs, out)
            if not err:
                err = judge(rel, rtc)
            result = (
                f"FAIL ({err})"
                if err
                else (
                    f"ok (webrtc: {rtc['dc']}/{rtc['dc'] + rtc['ws']} messages peer to peer, latency p50 {rtc['p50']:.1f} "
                    f"p95 {rtc['p95']:.1f} ms vs relay {rel['p50']:.1f}/{rel['p95']:.1f}; {rtc['dropped']} frames dropped, "
                    f"{rtc['asked']} key requests, {rtc['rx']}/{rtc['tx']} frames shown)"
                )
            )
            br.close()
    finally:
        srv.kill()
        open(os.path.join(D, "sessions.log"), "w").write("\n".join(out) + "\n")
    return finish(result)


def finish(result):
    print(f"party_webrtc_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
