#!/usr/bin/env python3
"""web.crt_math: the CRT math self-test (RT_SELFTEST_CRT, runtime/core/log.c) in the browser build.
The browser has no interpreter behind the lift: an arm of the CRT's x87 dispatcher that was never
lifted is skipped there and leaves the guest stack unbalanced (exp crashed the game at the first
scrum, 2026-10-12). Pass: "crt selftest: N cases, 0 wrong -> PASS" in the page's console.
One summary line; console in logs/web_selftest.log."""

import os
import re
import subprocess
import sys

from playwright.sync_api import sync_playwright

os.chdir(os.path.join(os.path.dirname(__file__), "..", ".."))
port = 18093
srv = subprocess.Popen(
    [sys.executable, "tools/web/serve.py", "build/web", str(port)], stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT
)
log = []
try:
    with sync_playwright() as p:
        br = p.chromium.launch(
            channel="chrome",
            headless=True,
            args=["--enable-unsafe-swiftshader", "--use-angle=swiftshader", "--mute-audio"],
        )
        pg = br.new_page()
        pg.on("console", lambda m: log.append(m.text))
        pg.goto(f"http://127.0.0.1:{port}/index.html?log&env=RT_SELFTEST_CRT=1")
        for _ in range(240):
            if any(re.search(r"crt selftest: \d+ cases", l) for l in log):
                break
            pg.wait_for_timeout(500)
finally:
    srv.kill()
    open("logs/web_selftest.log", "w").write("\n".join(log))
res = next((l for l in log if re.search(r"crt selftest: \d+ cases", l)), None)
if res and "0 wrong -> PASS" in res:
    print(f"web_selftest: ok ({res.split(': ', 1)[1]})")
    sys.exit(0)
print(f"web_selftest: FAIL ({res or 'no result'}; logs/web_selftest.log)")
sys.exit(1)
