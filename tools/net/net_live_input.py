#!/usr/bin/env python3
"""Netplay with live input in real browser pages: the relay server serves build/web, two Chrome pages
(Playwright, headless) join one room. Page 1 is the host: its real mouse clicks and keys are the
game's own keyboard and mouse, so they must reach the game on BOTH pages, at the same frame. Page 2
is a friend: its keyboard is its controller (S = Cross).
Pass: page 1's click and key show in both games, page 2's key reaches its pad, and both pages keep
printing equal state hashes afterwards.
One summary line; console logs in logs/net_live{1,2}.log."""

import os
import re
import subprocess
import sys
import time

from playwright.sync_api import sync_playwright

os.chdir(os.path.join(os.path.dirname(__file__), "..", ".."))
port = 18093
subprocess.run(["go", "build", "-o", "../build/rugby08-server", "."], cwd="server", check=True)
srv = subprocess.Popen(
    ["build/rugby08-server", "-addr", f"127.0.0.1:{port}", "-dir", "build/web"],
    stdout=open("logs/net_live_server.log", "w"),
    stderr=subprocess.STDOUT,
)
logs = [[], []]
try:
    with sync_playwright() as p:
        br = p.chromium.launch(
            channel="chrome",
            headless=True,
            args=["--enable-unsafe-swiftshader", "--use-angle=swiftshader", "--mute-audio"],
        )
        pages = []
        for i in range(2):
            pg = br.new_context().new_page()
            pg.on("console", lambda m, i=i: logs[i].append(m.text))
            pg.goto(
                f"http://127.0.0.1:{port}/index.html?log&env=RT_LOG_INPUT=1&net=auto&room=L{os.getpid()}&players=2&name=P{i + 1}"
            )
            pages.append(pg)

        def wait(pred, secs):
            end = time.time() + secs
            while time.time() < end:
                if pred():
                    return True
                pages[0].wait_for_timeout(500)
            return False

        if not wait(lambda: all(any("session started" in l for l in lg) for lg in logs), 120):
            print("net_live_input: FAIL (sessions did not start)")
            sys.exit(1)
        box = pages[0].locator("canvas").bounding_box()  # the mouse on one page only: held clicks and moves
        cx, cy = box["x"] + box["width"] / 2, box["y"] + box["height"] / 2
        for j in range(3):  # (a real click once skipped the legal screen on one side)
            pages[0].mouse.move(cx + 40 * j, cy)
            pages[0].mouse.down()
            pages[0].wait_for_timeout(700)
            pages[0].mouse.up()
            pages[0].mouse.down(button="right")
            pages[0].wait_for_timeout(300)
            pages[0].mouse.up(button="right")
        pages[0].keyboard.press("CapsLock")
        for key in ("s", "ArrowLeft"):
            pages[0].keyboard.down(key)
            pages[0].wait_for_timeout(400)
            pages[0].keyboard.up(key)

        def seen(i, pat):
            return any(re.search(pat, l) for l in logs[i])

        host = wait(
            lambda: all(seen(i, r"^input: mouse button 1 down") and seen(i, r"^input: key S down") for i in range(2)),
            60,
        )
        pages[1].locator("canvas").focus()
        pages[1].keyboard.down("s")
        pages[1].wait_for_timeout(400)
        pages[1].keyboard.up("s")
        friend = wait(lambda: seen(1, r"local input frame \d+ buttons 0001"), 60)
        got = host and friend

        def hashes(lg):
            return list(
                dict.fromkeys(l for l in lg if l.startswith("netplay: frame "))
            )  # each line is printed twice (stdout, stderr)

        n0 = len(hashes(logs[0]))
        synced = wait(lambda: len(hashes(logs[0])) >= n0 + 3 and len(hashes(logs[1])) >= n0 + 3, 300)
        h0, h1 = hashes(logs[0]), hashes(logs[1])
        k = min(len(h0), len(h1))
        same = synced and h0[:k] == h1[:k] and not any("DESYNC" in l for lg in logs for l in lg)
        for i in range(2):
            open(f"logs/net_live{i + 1}.log", "w").write("\n".join(logs[i]))
        ok = got and same
        print(
            f"net_live_input: {'ok' if ok else 'FAIL'} (host keyboard+mouse in both games: {host}, friend key on its pad: {friend}, {k} equal hashes)"
        )
        sys.exit(0 if ok else 1)
finally:
    srv.kill()
