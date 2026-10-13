#!/usr/bin/env python3
"""The browser friend of tools/net/ingame_ui_check.sh (Playwright, headless Chrome, system Python): opens the
web build from the relay at PORT, gets past the title screens, opens the in-game Online screens (F2),
JOIN A GAME, types CODE and joins. Its console goes to OUT; prints one line; exit 0 = it joined room CODE.

  tools/net/ingame_ui_web.py PORT CODE OUT"""

import sys
import time

from playwright.sync_api import sync_playwright


def main(port, code, out):
    lines = []
    ok = False
    with sync_playwright() as p:
        br = p.chromium.launch(
            channel="chrome",
            headless=True,
            args=["--mute-audio"],
        )
        page = br.new_page()
        page.on("console", lambda m: lines.append(m.text))
        page.goto(f"http://127.0.0.1:{port}/index.html?log&env=RT_MS_QUIT_AT=100000")  # traces the game's state

        def wait(pat, secs):
            end = time.time() + secs
            while time.time() < end:
                if any(pat in x for x in lines):
                    return True
                page.wait_for_timeout(500)
            return False

        try:
            if not wait("ms: game state 008CD620", 600):  # the front end
                return finish(out, lines, "the web game never reached its menus")
            page.wait_for_timeout(15000)
            canvas = page.locator("canvas")
            canvas.focus()
            canvas.click()
            page.wait_for_timeout(3000)
            page.keyboard.press("Enter")  # past the title screen
            page.wait_for_timeout(8000)
            for _ in range(3):  # a loaded machine runs the browser game slowly: until the screens are open
                page.keyboard.press("F2")
                if wait("ms: ui screen 1", 90):
                    break
            else:
                return finish(out, lines, "F2 did not open the Online screens")
            for key in ["ArrowDown", "Enter"] + list(code) + ["Enter"]:
                page.keyboard.press(key)
                page.wait_for_timeout(1500)
            ok = wait(f"ms: ui room {code} as player", 180)
            page.keyboard.press("F2")
            page.wait_for_timeout(2000)
        finally:
            br.close()
    return finish(out, lines, None if ok else f"the web friend did not join room {code}")


def finish(out, lines, why):
    open(out, "w").write("\n".join(lines))
    print(f"ingame_ui_web: {'FAIL (' + why + ')' if why else 'ok'}")
    return 1 if why else 0


if __name__ == "__main__":
    sys.exit(main(int(sys.argv[1]), sys.argv[2], sys.argv[3]))
