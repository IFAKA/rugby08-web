#!/usr/bin/env python3
"""pt.own_files: on the party site each player brings their own Rugby 08 (platform/web/js/own_copy.js).

The server serves build/site (tools/web/site_manifest.py: the program and a manifest of the game files, no
game file). Headless Chrome, muted:
  wrong folder   a folder that is not Rugby 08 is refused with the player's message ("is not Rugby 08");
  other version  the game folder with one game file changed is refused ("different version");
  first visit    the player picks the real game folder: every chunk checked and kept in the browser, the
                 game boots to its main menu reading only those (the site has no chunk: a fetch would 404);
  next visit     a reload boots to the main menu without asking.
One line; details in logs/own_copy/."""

import os
import shutil
import socket
import subprocess
import sys
import time

from playwright.sync_api import sync_playwright
from web_party import ENV, ROOT, Player, to_menus
from web_route import browser

D = os.path.join(ROOT, "logs", "own_copy")
GAME = os.path.join(ROOT, "Game Files")
SITE = os.path.join(ROOT, os.environ.get("OWN_SITE", "build/site"))  # deploy_check: the package's
PASSWORD = os.environ.get("OWN_PASSWORD", "")  # the site is private: players come in by the party link


class SitePlayer(Player):
    """A Player in a context of its own (own browser storage) that may stop at the folder screen."""

    def __init__(self, ctx, name, port):
        self.name, self.lines, self.ctx = name, [], ctx
        self.page = ctx.new_page()
        self.page.on("console", lambda m: self.lines.append(m.text))
        self.url = f"http://127.0.0.1:{port}/index.html?log&env={ENV}"
        if PASSWORD:  # the party link: refused without it, then its cookie lets every request in
            self.page.goto(f"http://127.0.0.1:{port}/index.html")
            if "private" not in self.page.inner_text("body"):
                raise RuntimeError("the private site let a page in without the party link")
            self.page.goto(f"http://127.0.0.1:{port}/k/{PASSWORD}")
        self.page.goto(self.url)

    def pick(self, folder):
        self.page.wait_for_selector("#owncopy-input", state="attached", timeout=120000)
        self.page.set_input_files("#owncopy-input", folder)

    def says(self, text, secs):
        loc = self.page.locator("#owncopy")
        return self.wait_for(lambda: loc.count() and text in loc.inner_text(), secs)


def server():
    if subprocess.run(
        ["go", "build", "-o", "../build/rugby08-server", "."], cwd=os.path.join(ROOT, "server")
    ).returncode:
        return None, 0
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    exe = os.path.join(ROOT, "build", "rugby08-server")
    srv = subprocess.Popen(
        [exe, "-addr", f"127.0.0.1:{port}", "-dir", SITE, "-logdir", D, "-password", PASSWORD],
        stdout=open(os.path.join(D, "server.log"), "w"),
        stderr=subprocess.STDOUT,
    )
    time.sleep(1)
    return (srv, port) if srv.poll() is None else (None, 0)


def folders():
    """A folder that is not the game, and the game folder with its first game file changed (hard links)."""
    wrong = os.path.join(D, "wrong")
    os.makedirs(wrong)
    open(os.path.join(wrong, "README.txt"), "w").write("not a game\n")
    other = os.path.join(D, "other", "Rugby 08")
    for sub in ("Audio", "Data"):
        for r, _, fs in os.walk(os.path.join(GAME, sub)):
            for f in sorted(fs):
                p = os.path.join(r, f)
                q = os.path.join(other, os.path.relpath(p, GAME))
                os.makedirs(os.path.dirname(q), exist_ok=True)
                os.link(p, q)
    first = os.path.join(other, "Audio", sorted(os.listdir(os.path.join(GAME, "Audio")))[0])
    size = os.path.getsize(first)
    os.unlink(first)
    with open(first, "wb") as f:
        f.write(b"\0" * size)
    return wrong, os.path.dirname(other)


def run(br, port, steps):
    wrong, other = folders()
    p = SitePlayer(br.new_context(), "wrong", port)
    p.pick(wrong)
    if not p.says("is not Rugby 08", 60):
        return "a folder that is not the game was not refused"
    steps.write("wrong folder: refused\n")
    p.pick(other)
    if not p.says("different version", 600):
        return "a changed game file was not refused"
    steps.write("other version: refused\n")
    p.ctx.close()

    p = SitePlayer(br.new_context(), "player", port)
    t0 = time.time()
    p.pick(GAME)
    if not p.wait("owncopy: stored", 1200):
        return "the game folder was not stored: " + str([x for x in p.lines if "owncopy" in x][-1:])
    steps.write(f"first visit: stored in {time.time() - t0:.0f} s\n")
    err = to_menus(p)
    if err:
        return "first visit: " + err
    steps.write(f"first visit: main menu, {p.frames()} frames\n")
    p.lines.clear()
    p.page.goto(p.url)
    if not p.wait_for(lambda: p.page.evaluate("Module.rtFrames | 0") > 0, 300):
        return "next visit: no frame"
    if p.page.locator("#owncopy").count():
        return "next visit: asked for the folder again"
    err = to_menus(p)
    if err:
        return "next visit: " + err
    bad = [x for x in p.lines if "gamefs" in x and "fail" in x]
    if bad:
        return "next visit: " + bad[0]
    steps.write("next visit: main menu without asking\n")
    return None


def main():
    shutil.rmtree(D, ignore_errors=True)
    os.makedirs(D)
    if not os.path.isfile(os.path.join(SITE, "data", "manifest.json")):
        print("own_copy_check: FAIL (no build/site: run tools/web/site_manifest.py)")
        return 1
    srv, port = server()
    if not srv:
        print("own_copy_check: FAIL (the server did not start, logs/own_copy/server.log)")
        return 1
    steps = open(os.path.join(D, "steps.log"), "w")
    try:
        with sync_playwright() as pw:
            br = browser(pw)
            err = run(br, port, steps)
            br.close()
    finally:
        srv.terminate()
        shutil.rmtree(os.path.join(D, "other"), ignore_errors=True)
    print(f"own_copy_check: {'FAIL (' + err + ')' if err else 'ok'} (logs/own_copy/steps.log)")
    return 1 if err else 0


if __name__ == "__main__":
    sys.exit(main())
