"""Drive the browser game through its menus by what it shows, not by timings: the page's trace says
which front-end screen is current ("ms: fe screen ... id XXXX", runtime/debug/match_quit.c, on with
RT_MS_QUIT_AT) and the game's state ("ms: game state VT"); each step presses the key that screen
wants and waits for the next screen. Headless pages run at any speed, so this works on a slow machine.

  browser(pw) -> headless Chrome as every browser check runs it (muted; the real GPU: headless Chrome
                 renders WebGL with Metal by default, ~10x SwiftShader's frame rate in a match)
  screen(lines) -> the current screen id (int) or None
  play_now(page, lines, log, until=ID) -> None when the page is in a match (BackendState), or on screen
                 ID (e.g. TEAM_SELECT), else the reason

Keys (docs/CONTROLS.md): 's' Cross (accept), Left/Right move the keyboard's icon in Team Select."""

import re
import time

BACKEND = "ms: game state 008CC6F0"
TEAM_SELECT = 0x0006


def browser(pw):
    return pw.chromium.launch(channel="chrome", headless=True, args=["--mute-audio"])


def screen(lines):
    for x in reversed(lines):
        m = re.search(r"ms: fe screen \w+ \(vt \w+, id ([0-9A-F]{4})\)", x)
        if m:
            return int(m.group(1), 16)
    return None


def tap(page, key, hold=120):
    """A key held for a few frames: the game samples its keyboard once a frame."""
    page.keyboard.down(key)
    page.wait_for_timeout(hold)
    page.keyboard.up(key)


def changed(page, lines, old, secs):
    end = time.time() + secs
    while time.time() < end:
        if screen(lines) != old or any(BACKEND in x for x in lines):
            return True
        page.wait_for_timeout(250)
    return False


def play_now(page, lines, log=None, secs=600, until=None):
    """From the title or the main menu into a PLAY NOW match, the keyboard on the home side."""
    end, moved = time.time() + secs, False
    while time.time() < end:
        if any(BACKEND in x for x in lines):
            return None
        s = screen(lines)
        if until is not None and s == until:
            return None
        if log:
            log(f"route: screen {s:04X}" if s is not None else "route: no screen yet")
        if s == TEAM_SELECT and not moved and until is None:
            tap(page, "ArrowLeft")
            page.wait_for_timeout(1500)
            moved = True
            continue
        if s is not None:
            tap(page, "s")  # accept: the title, PLAY NOW (first item), Team Select, Stadium, popups
        changed(page, lines, s, 20)
        page.wait_for_timeout(500)
    return f"no match within {secs} s (last screen {screen(lines)})"
