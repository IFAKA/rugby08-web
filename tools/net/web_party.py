"""Browser party players for the online checks (Playwright, system Python): a page of the web build per
player, its console (the game's trace) collected, keys held a few frames, the relay on a free port.

  relay(D) -> (process, port)       server/ built and started on a free port, serving build/web (log in D)
  Player(browser, name, port)       a page in its own context; .lines its console; .wait(pat, s) .key(k)
  to_menus(p)                       booted, clicked (the AudioContext starts), past the title, F2 open
  host_room(p) / join_room(p, code) -> the room code / None (in-game Online screens, closed after)"""

import os
import re
import socket
import subprocess
import time

from web_route import screen, tap

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ENV = "RT_MS_QUIT_AT=1000000,RT_MS_POS=10"  # the screen/state trace, the match numbers every 10 frames


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
    def __init__(self, br, name, port, query=""):
        self.name, self.lines = name, []
        self.page = br.new_context().new_page()
        self.page.on("console", lambda m: self.lines.append(m.text))
        self.page.goto(f"http://127.0.0.1:{port}/index.html?log&env={ENV}{query}")

    def wait(self, pat, secs):
        end = time.time() + secs
        while time.time() < end:
            hit = next((x for x in self.lines if pat in x), None)
            if hit:
                return hit
            self.page.wait_for_timeout(500)
        return None

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


def to_menus(p):
    if not p.wait("ms: game state 008CD620", 600):
        return f"{p.name} never reached the menus"
    p.page.wait_for_timeout(5000)
    p.page.locator("canvas").click()  # the AudioContext starts here
    p.page.wait_for_timeout(3000)
    for _ in range(10):  # past the title, as a player is before joining a party
        if screen(p.lines) not in (None, 0x0002):
            break
        p.key("s", 3000)
    return open_online(p)


def open_online(p):
    for _ in range(3):  # a slow page: until the screens are open (a second F2 would close them)
        p.key("F2", 0)
        if p.wait("ms: ui screen 1", 90):
            return None
    return f"{p.name}: F2 did not open the Online screens"


def host_room(p):
    p.key("Enter")
    hit = p.wait("as player 1 (host)", 60)
    if not hit:
        return None
    p.key("F2", 3000)
    return hit.split("ms: ui room ")[1].split()[0]


def join_room(p, code):
    for k in ["ArrowDown", "Enter"] + list(code) + ["Enter"]:
        p.key(k)
    if not p.wait(f"ms: ui room {code} as player", 180):
        return None
    p.key("F2", 3000)
    return code
