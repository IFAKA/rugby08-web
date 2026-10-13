#!/usr/bin/env python3
"""A browser player for the online checks (Playwright, headless Chrome, system Python): opens the web build
from the relay at PORT, gets past the title screens and uses the in-game Online screens (F2).

  tools/net/web_player.py PORT OUT host [--play-now] [--secs S]    host a room; print its code; optionally
                                                                   play PLAY NOW into a match (the keyboard
                                                                   on the home side) and stay S seconds
  tools/net/web_player.py PORT OUT join CODE [--secs S]            join CODE and stay S seconds

The page's console (game trace, ?env=ENV) goes to OUT. Prints "web_player: room CODE" when in a room and
one final line; exit 0 = in the room (and, with --play-now, in a match)."""

import sys
import time

from playwright.sync_api import sync_playwright

ENV = "RT_MS_QUIT_AT=100000,RT_MS_POS=10"  # the game's state changes and the per-frame match numbers
# match_play.txt's PLAY NOW route from the main menu, with its gaps (frames / 60, a little longer)
PLAY_NOW = [("s", 6), ("s", 6), ("s", 6), ("s", 9), ("s", 15), ("s", 9), ("ArrowLeft", 7), ("s", 9), ("s", 9)]


def run(port, out, role, code, play_now, secs):
    lines, result = [], "FAIL (setup)"
    with sync_playwright() as p:
        br = p.chromium.launch(
            channel="chrome",
            headless=True,
            args=["--mute-audio"],
        )
        page = br.new_page()
        page.on("console", lambda m: lines.append(m.text))
        page.goto(f"http://127.0.0.1:{port}/index.html?log&env={ENV}")

        def wait(pat, s):
            end = time.time() + s
            while time.time() < end:
                hit = next((x for x in lines if pat in x), None)
                if hit:
                    return hit
                page.wait_for_timeout(500)
            return None

        def key(k, pause=1500):
            page.keyboard.press(k)
            page.wait_for_timeout(pause)

        try:
            if not wait("ms: game state 008CD620", 600):
                return finish(out, lines, "FAIL (the game never reached its menus)")
            page.wait_for_timeout(10000)
            canvas = page.locator("canvas")
            canvas.focus()
            canvas.click()
            page.wait_for_timeout(3000)
            key("Enter", 8000)  # past the title screen
            for _ in range(3):  # a slow page: until the screens are open (a second F2 would close them)
                key("F2", 0)
                if wait("ms: ui screen 1", 90):
                    break
            else:
                return finish(out, lines, "FAIL (F2 did not open the Online screens)")
            if role == "host":
                key("Enter")
                hit = wait("as player 1 (host)", 60)
                if not hit:
                    return finish(out, lines, "FAIL (no room)")
                code = hit.split("ms: ui room ")[1].split()[0]
            else:
                for k in ["ArrowDown", "Enter"] + list(code) + ["Enter"]:
                    key(k)
                if not wait(f"ms: ui room {code} as player", 180):
                    return finish(out, lines, f"FAIL (did not join {code})")
            print(f"web_player: room {code}", flush=True)
            key("F2", 3000)
            if play_now:
                for k, pause in PLAY_NOW:
                    key(k, pause * 1000)
                if not wait("ms: game state 008CC6F0", 300):  # BackendState: in a match
                    return finish(out, lines, f"FAIL (in room {code}, but no match)")
                for _ in range(4):  # the intro
                    key("s", 4000)
            result = "ok"
            page.wait_for_timeout(int(secs * 1000))
        finally:
            br.close()
    return finish(out, lines, result)


def finish(out, lines, result):
    open(out, "w").write("\n".join(lines))
    print(f"web_player: {result}", flush=True)
    return 0 if result == "ok" else 1


def main(argv):
    port, out, role = int(argv[0]), argv[1], argv[2]
    code = argv[3] if role == "join" else None
    secs = float(argv[argv.index("--secs") + 1]) if "--secs" in argv else 60
    return run(port, out, role, code, "--play-now" in argv, secs)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
