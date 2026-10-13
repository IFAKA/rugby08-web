"""Browser party players for the online checks (Playwright, system Python): a page of the web build per
player, its console (the game's trace) collected, keys held a few frames, the relay on a free port.

  relay(D) -> (process, port)       server/ built and started on a free port, serving build/web (log in D)
  Player(browser, name, port)       a page in its own context; .lines its console; .wait(pat, s) .key(k)
  to_menus(p)                       booted, clicked (the AudioContext starts), past the title: main menu
  host_room(p) / join_room(p, code) -> the party's code / None (the game's own ONLINE screens; the host
                                    back in its menus, a friend in the party's lobby)"""

import os
import re
import socket
import subprocess
import time

from web_route import screen, tap

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ENV = "RT_MS_QUIT_AT=1000000,RT_MS_POS=10"  # the screen/state trace, the match numbers every 10 frames
MAIN, ONLINE, PARTY, JOIN = 0x32, 7100, 7101, 7102  # front-end screens (runtime/include/fe_party.h)


def relay(d):
    build = ["go", "build", "-o", "../build/rugby08-server", "."]
    if subprocess.run(build, cwd=os.path.join(ROOT, "server")).returncode:
        return None, 0
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    cmd = [
        os.path.join(ROOT, "build", "rugby08-server"),
        "-addr",
        f"127.0.0.1:{port}",
        "-dir",
        os.path.join(ROOT, "build", "web"),
    ]
    srv = subprocess.Popen(cmd, stdout=open(os.path.join(d, "server.log"), "w"), stderr=subprocess.STDOUT)
    time.sleep(1)
    return (srv, port) if srv.poll() is None else (None, 0)


class Player:
    def __init__(self, br, name, port, query="", env=""):
        self.name, self.lines = name, []
        self.page = br.new_context().new_page()
        self.page.on("console", lambda m: self.lines.append(m.text))
        extra = "," + env if env else ""
        self.page.goto(f"http://127.0.0.1:{port}/index.html?log&env={ENV}{extra}{query}")

    def wait(self, pat, secs):
        end = time.time() + secs
        while time.time() < end:
            hit = next((x for x in self.lines if pat in x), None)
            if hit:
                return hit
            self.page.wait_for_timeout(500)
        return None

    def wait_for(self, cond, secs):
        end = time.time() + secs
        while time.time() < end:
            if cond():
                return True
            self.page.wait_for_timeout(500)
        return False

    def key(self, k, pause=1500):
        tap(self.page, k)
        self.page.wait_for_timeout(pause)

    def frames(self):
        return self.page.evaluate("Module.rtFrames | 0")

    def last(self, rx):
        for x in reversed(self.lines):
            m = re.search(rx, x)
            if m:
                return float(m.group(1))
        return 0.0

    def count(self, pat):
        return sum(1 for x in self.lines if pat in x)


def wait_screen(p, sid, secs=30):
    return p.wait_for(lambda: screen(p.lines) == sid, secs)


def to_menus(p):
    """Booted, clicked into (the AudioContext starts), past the title: in the main menu."""
    if not p.wait("ms: game state 008CD620", 600):
        return f"{p.name} never reached the menus"
    p.page.wait_for_timeout(3000)
    p.page.locator("canvas").click()
    for _ in range(12):
        if screen(p.lines) == MAIN:
            return None
        p.key("s", 3000)
    return f"{p.name}: no main menu"


def host_room(p):
    """From the main menu: ONLINE, CREATE PARTY, CONTINUE (back in the menus). The party's code."""
    for k in ("ArrowDown", "s"):
        p.key(k)
    if not wait_screen(p, ONLINE):
        return None
    p.key("s", 3000)
    hit = p.wait("as player 1 (host)", 60)
    if not hit or not wait_screen(p, PARTY):
        return None
    p.key("s", 3000)  # CONTINUE
    return hit.split("ms: ui room ")[1].split()[0] if wait_screen(p, MAIN) else None


def join_room(p, code):
    """From the main menu: ONLINE, JOIN PARTY, the code typed, Return: in the party's lobby."""
    for k in ("ArrowDown", "s", "ArrowDown", "s"):
        p.key(k)
    if not wait_screen(p, JOIN):
        return None
    for k in list(code) + ["Enter"]:
        p.key(k, 400)
    joined = p.wait(f"ms: ui room {code} as player", 120)  # then its lobby, or at once the host's match
    ok = joined and p.wait_for(
        lambda: screen(p.lines) == PARTY or any("joining the host's match" in x for x in p.lines), 30
    )
    return code if ok else None
