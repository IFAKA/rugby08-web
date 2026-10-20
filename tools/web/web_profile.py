#!/usr/bin/env python3
"""CPU profile of the browser build (headless Chrome, DevTools Profiler on the page's main thread,
where the game's main guest thread runs). The wasm keeps its function names (--profiling-funcs), so
self time is reported per lifted function.
  tools/web/web_profile.py [--start 'CONSOLE TEXT'] [--seconds S] [--env RT_A=1,RT_B=2] [--top N]
Profiling starts when TEXT first appears in the console (default: the first VP6 movie frame).
One summary line (top 3 by self time); the full table in logs/web_profile.log."""

import argparse
import collections
import os
import socket
import subprocess
import sys
import time


def free_port():  # checks may run side by side
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


from playwright.sync_api import sync_playwright

ap = argparse.ArgumentParser()
ap.add_argument("--start", default="vp6 frame decoded")
ap.add_argument("--seconds", type=float, default=6)
ap.add_argument("--env", default="")
ap.add_argument("--top", type=int, default=40)
ap.add_argument("--timeout", type=float, default=180)
a = ap.parse_args()

os.chdir(os.path.join(os.path.dirname(__file__), "..", ".."))
port = free_port()
srv = subprocess.Popen(
    [sys.executable, "tools/web/serve.py", "build/web", str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT
)
log, out = [], []
try:
    with sync_playwright() as p:
        br = p.chromium.launch(
            channel="chrome",
            headless=True,
            args=["--enable-unsafe-swiftshader", "--use-angle=swiftshader", "--mute-audio"],
        )
        pg = br.new_page()
        pg.on("console", lambda m: log.append(m.text))
        cdp = pg.context.new_cdp_session(pg)
        cdp.send("Profiler.enable")
        cdp.send("Profiler.setSamplingInterval", {"interval": 500})
        q = "?log" + (f"&env={a.env}" if a.env else "")
        pg.goto(f"http://127.0.0.1:{port}/index.html{q}")
        t0 = time.time()
        while not any(a.start in l for l in log):
            if time.time() - t0 > a.timeout:
                raise SystemExit(f"web_profile: FAIL ('{a.start}' not seen in {a.timeout:.0f} s)")
            pg.wait_for_timeout(100)
        f0 = (pg.evaluate("Module.rtFrames|0"), time.time())
        cdp.send("Profiler.start")
        pg.wait_for_timeout(int(a.seconds * 1000))
        prof = cdp.send("Profiler.stop")["profile"]
        f1 = (pg.evaluate("Module.rtFrames|0"), time.time())
finally:
    srv.kill()

nodes = {n["id"]: n for n in prof["nodes"]}
dt = collections.Counter()
for sid, d in zip(prof["samples"], prof["timeDeltas"]):
    dt[sid] += d
self_t = collections.Counter()
for sid, us in dt.items():
    cf = nodes[sid]["callFrame"]
    self_t[cf["functionName"] or f"({cf['url'].rsplit('/', 1)[-1]})"] += us
total = sum(self_t.values()) or 1
idle = sum(v for k, v in self_t.items() if k in ("(idle)", "(program)", "(garbage collector)"))
fps = (f1[0] - f0[0]) / (f1[1] - f0[1])
out.append(f"window {a.seconds:.1f} s from '{a.start}': {fps:.1f} presents/s, busy {100 * (total - idle) / total:.0f}%")
for name, us in self_t.most_common(a.top):
    out.append(f"{100 * us / total:6.2f}%  {us / 1000:9.1f} ms  {name}")
open("logs/web_profile.log", "w").write("\n".join(out) + "\n\n--- console ---\n" + "\n".join(log[-200:]))
top3 = ", ".join(f"{n} {100 * u / total:.0f}%" for n, u in self_t.most_common(4) if n != "(idle)")
print(f"web_profile: {fps:.1f} fps; top: {top3} (logs/web_profile.log)")
