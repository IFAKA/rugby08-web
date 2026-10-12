#!/usr/bin/env python3
"""Drop-in online play in real browser pages (Playwright, headless Chrome), the way players use it:
page 1 opens the game (no lobby: it boots straight in and its room is open), the invite link is read
from its Friends panel, and page 2 opens that link JOIN_AFTER seconds later. Pass: page 2 catches up
and says "You're in", a key pressed on page 2 reaches its pad, both pages keep equal state hashes,
and page 1 plays on after page 2 closes. One summary line; console logs in logs/net_dropin{1,2}.log."""
import os, re, subprocess, sys, time
from playwright.sync_api import sync_playwright

os.chdir(os.path.join(os.path.dirname(__file__), ".."))
port, join_after = 18094, float(os.environ.get("JOIN_AFTER", "20"))
subprocess.run(["go", "build", "-o", "../build/rugby08-server", "."], cwd="server", check=True)
srv = subprocess.Popen(["build/rugby08-server", "-addr", f"127.0.0.1:{port}", "-dir", "build/web"],
                       stdout=open("logs/net_dropin_server.log", "w"), stderr=subprocess.STDOUT)
logs = [[], []]
def fail(why):
    for i in range(2): open(f"logs/net_dropin{i+1}.log", "w").write("\n".join(logs[i]))
    print(f"net_dropin: FAIL ({why})"); sys.exit(1)
try:
    with sync_playwright() as p:
        br = p.chromium.launch(channel="chrome", headless=True,
                               args=["--enable-unsafe-swiftshader", "--use-angle=swiftshader", "--mute-audio"])
        pages = []
        def open_page(url):
            i = len(pages)
            pg = br.new_context(permissions=["clipboard-read", "clipboard-write"]).new_page()
            pg.on("console", lambda m, i=i: logs[i].append(m.text))
            pg.goto(url); pages.append(pg); return pg
        def wait(pred, secs):
            end = time.time() + secs
            while time.time() < end:
                if pred(): return True
                pages[0].wait_for_timeout(500)
            return False
        def has(i, pat): return any(re.search(pat, l) for l in logs[i])
        a = open_page(f"http://127.0.0.1:{port}/index.html?log")
        if not wait(lambda: has(0, "session started as player 1"), 120): fail("page 1 did not start its game")
        a.wait_for_selector("#friends-btn:not([hidden])", timeout=30000)
        a.click("#friends-btn")
        link = a.input_value("#invite")
        if "?room=" not in link: fail(f"no invite link ({link!r})")
        a.click("#friends-btn")
        a.wait_for_timeout(int(join_after * 1000))
        b = open_page(link + "&log")
        if not wait(lambda: has(1, r"caught up at frame \d+"), 600): fail("page 2 did not catch up")
        jm = re.search(r"player 2 joins at frame (\d+)", "\n".join(logs[1])); joined_at = int(jm.group(1)) if jm else -1
        try: b.wait_for_selector("#toast:not([hidden])", timeout=5000); toast = b.inner_text("#toast")
        except Exception: toast = ""
        if "You're in" not in toast: fail(f"no 'You're in' toast on page 2 ({toast!r})")
        b.locator("canvas").focus()
        b.keyboard.down("s"); b.wait_for_timeout(400); b.keyboard.up("s")
        if not wait(lambda: has(1, r"local input frame \d+ buttons 0001"), 60): fail("page 2's key did not reach its pad")
        def hashes(i): return [l for l in logs[i] if l.startswith("netplay: frame ")]
        n0 = len(hashes(0))
        if not wait(lambda: len(hashes(0)) >= n0 + 3 and len(hashes(1)) >= len(hashes(0)), 300): fail("hashes stopped")
        h0, h1 = hashes(0), hashes(1)
        k = min(len(h0), len(h1))
        if h0[:k] != h1[:k] or any("DESYNC" in l for lg in logs for l in lg): fail(f"state hashes differ ({k} compared)")
        b.close()
        n1 = len(hashes(0))
        if not wait(lambda: has(0, "player 2 leaves") and len(hashes(0)) >= n1 + 2, 120): fail("page 1 stopped after page 2 left")
        for i in range(2): open(f"logs/net_dropin{i+1}.log", "w").write("\n".join(logs[i]))
        print(f"net_dropin: ok (joined at frame {joined_at} after {join_after:.0f}s, {k} equal hashes, host plays on after the friend left)")
finally:
    srv.kill()
