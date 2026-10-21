#!/usr/bin/env python3
"""ls.match: a PLAY NOW match in a lockstep party is the same match on every screen.

Two pages (headless Chrome, muted, relay on a free port). The host creates a party, the friend joins by
code; the host takes PLAY NOW to Team Select, its keyboard goes home, the friend's away; into the match.
Both play (Cross now and then: each side answers its own set pieces). Measured:
  sync     the state hashes ("netplay: frame F hash H") are equal in both games;
  picture  the game's picture is the same at the same frame ("pix: frame F HASH", RT_PIX: the back
           buffer before the local overlays): menus, intro cutscene, set-piece banners, referee; except
           while a game leaves the other team's prompt out ("ms: own hud part N left out", own_hud.c);
  pause    the friend's Esc pauses both games (screen 0x71), the host's screen says who paused (the menu is
           the friend's), its Cross on RESUME resumes both;
  presses  the friend's presses act in the match: its pause, and the away side's set pieces it answers.
One line; details in logs/ls_match/."""

import os
import re
import sys

from playwright.sync_api import sync_playwright
from web_party import ROOT, Player, relay, to_menus, wait_screen
from web_route import BACKEND, TEAM_SELECT, browser, play_now, screen

sys.path.insert(0, os.path.dirname(__file__))
from ls_party_check import create, in_sync, join  # noqa: E402

D = os.path.join(ROOT, "logs", "ls_match")
PIX = re.compile(r"pix: frame (\d+) (\S+)")
PAUSED = 0x71


def pixels(p):
    """This page's session only (a friend's own game before it joined is another session)."""
    start = max((i for i, x in enumerate(p.lines) if "netplay: session started" in x), default=0)
    return {int(m.group(1)): m.group(2) for x in p.lines[start:] for m in [PIX.search(x)] if m}


HUD = re.compile(r"ms: own hud part \d+ (left out|shown) .* at frame (\d+)")


def own_hud(p):
    """The frames this game left another team's prompt out of its picture (own_hud.c: the kick aim of the
    other team's kicker), as (from, to) pairs: there the pictures differ by design."""
    spans, start = [], None
    for x in p.lines:
        m = HUD.search(x)
        if m and m.group(1) == "left out" and start is None:
            start = int(m.group(2))
        elif m and m.group(1) == "shown" and start is not None:
            spans.append((start, int(m.group(2))))
            start = None
    return spans + ([(start, 1 << 30)] if start is not None else [])


def same_picture(players, out, what, lo=0):
    ps = [pixels(p) for p in players]
    own = [s for p in players for s in own_hud(p)]
    mine = sorted(f for f in set.intersection(*(set(x) for x in ps)) if f >= lo)
    common = [f for f in mine if not any(a <= f <= b + 1 for a, b in own)]
    if len(common) < len(mine):
        out.append(f"{what}: {len(mine) - len(common)} pictures with one team's prompt left out of the other's")
    bad = [f for f in common if len({x[f] for x in ps}) > 1]
    out.append(f"{what}: {len(common)} pictures compared, {len(bad)} different" + (f" (first {bad[0]})" if bad else ""))
    for f in bad[:3]:  # where: the 16x12 blocks that differ (x,y), and whether the game's camera did
        rows = [next(x for x in reversed(p.lines) if f"pix: frame {f} " in x).split() for p in players]
        bl = [r[r.index("blocks") + 1] for r in rows]
        diff = [i // 4 for i in range(0, len(bl[0]), 4) if len({b[i : i + 4] for b in bl}) > 1]
        same_cam = len({r[r.index("cam") + 1] for r in rows}) == 1
        out.append(
            f"  frame {f}: blocks {[(i % 16, i // 16) for i in diff]}, game camera {'same' if same_cam else 'DIFFERENT'}"
        )
    return len(common) >= 10 and not bad


def phases(p):
    return [
        m.group(1)
        for x in p.lines
        for m in [re.search(r"ms: pos f \d+ t [\d.]+ clock \S+ score \S+ phase (\S+)", x)]
        if m
    ]


def play(players, secs):
    """Cross now and then: each side answers its own prompts (kick-offs, kicks, line-outs). Not in a
    scrum's formation: there Cross is the feed and engage, and pressed at random it resets the scrum."""
    for _ in range(int(secs / 1.5)):
        for p in players:
            if phases(players[0])[-1:] == ["Formation"]:
                p.page.wait_for_timeout(1500 // len(players))
            else:
                p.key("s", 1500 // len(players))


def into_match(host, friend, out):
    for p in (host, friend):
        err = to_menus(p)
        if err:
            return err
    code = create(host)
    r = join(friend, code or "")
    out.append(f"friend: {r}")
    if not r.startswith("caught up"):
        return f"the friend did not join: {r}"
    host.key("Escape", 2000)  # back in the main menu
    err = play_now(host.page, host.lines, until=TEAM_SELECT)
    if err or not wait_screen(friend, TEAM_SELECT, 60):
        return f"Team Select not reached on both ({err})"
    host.key("ArrowLeft", 2000)
    friend.key("ArrowRight", 3000)
    err = play_now(host.page, host.lines, ready=[friend.page])
    if err or not friend.wait(BACKEND, 60):
        return f"the match did not start on both ({err})"
    return None


def pause(host, friend, out):
    for _ in range(60):  # open play (the game does not pause inside a set piece's menus)
        if phases(host)[-1:] == ["Free"]:
            break
        play([host, friend], 1.5)
    friend.key("Escape", 3000)
    if not all(p.wait_for(lambda p=p: screen(p.lines) == PAUSED, 20) for p in (host, friend)):
        return "the friend's Esc did not pause both games"
    if not host.wait("ms: party role PAUSED BY PLAYER 2", 10):
        return "the host's screen did not say the friend paused (its own pause menu is the friend's)"
    friend.key("s", 3000)  # RESUME
    if not all(p.wait_for(lambda p=p: screen(p.lines) != PAUSED, 20) for p in (host, friend)):
        return "the friend's RESUME did not resume both games"
    out.append("pause: the friend paused and resumed both games")
    return None


def own_camera(host, friend, out):
    """C cycles the friend's own view: its picture changes, the host's and the match (hashes) do not; the
    sixth press is back at the game's camera, the same picture again."""
    n = len(friend.lines)
    friend.key("c", 1000)
    if not friend.wait_for(lambda: any("party toast CAMERA: CLOSER" in x for x in friend.lines[n:]), 10):
        return "C did not change the friend's camera", 0
    if any("party toast CAMERA" in x for x in host.lines):
        return "the friend's camera key reached the host", 0
    lo = max(pixels(friend)) + 1
    play([host, friend], 15)
    ps = [pixels(p) for p in (host, friend)]
    common = [f for f in set(ps[0]) & set(ps[1]) if f >= lo]
    same = sum(ps[0][f] == ps[1][f] for f in common)
    out.append(f"own camera: {len(common) - same}/{len(common)} pictures differ with the friend's view Closer")
    if len(common) < 10 or same > len(common) // 10:
        return "the friend's own view did not change its picture", 0
    for _ in range(5):
        friend.key("c", 600)
    if not friend.wait("party toast CAMERA: GAME CAMERA", 10):
        return "six presses of C did not come back to the game's camera", 0
    friend.page.wait_for_timeout(2000)
    lo = max(pixels(friend)) + 1
    play([host, friend], 15)
    return None if same_picture([host, friend], out, "back to the game camera", lo) else "the game camera differs", lo


def run(br, port, out):
    env = "RT_PIX=30" + (f",RT_PIX_DUMP={os.environ['RT_PIX_DUMP']}" if os.environ.get("RT_PIX_DUMP") else "")
    host, friend = Player(br, "host", port, env=env), Player(br, "friend", port, env=env)
    players = [host, friend]
    try:
        err = into_match(host, friend, out)
        if err:
            return err
        play(players, 60)
        if not in_sync(players, out, "first minute") or not same_picture(players, out, "first minute"):
            return "the two games differ in the first minute of the match"
        err = pause(host, friend, out)
        if err:
            return err
        err, lo = own_camera(host, friend, out)
        if err:
            return err
        play(players, 120)
        out.append(f"phases: {' '.join(dict.fromkeys(phases(host)))}")
        if not in_sync(players, out, "match"):
            return "the two games are not in sync"
        if not same_picture(players, out, "rest of the match", lo):
            return "the two games do not show the same picture"
        return None
    finally:
        for p in players:
            open(os.path.join(D, p.name + ".log"), "w").write("\n".join(p.lines))
            try:
                p.page.screenshot(path=os.path.join(D, p.name + ".png"), timeout=10000)
            except Exception as e:  # (a page that hangs must not hide the result)
                out.append(f"{p.name}: no screenshot ({e.__class__.__name__})")


def main():
    os.makedirs(D, exist_ok=True)
    srv, port = relay(D)
    if not srv:
        return finish("FAIL (relay did not build or start)")
    out, err = [], None
    try:
        with sync_playwright() as pw:
            br = browser(pw)
            err = run(br, port, out)
            br.close()
    finally:
        srv.kill()
        open(os.path.join(D, "steps.log"), "w").write("\n".join(out) + "\n")
    return finish(f"FAIL ({err})" if err else f"ok ({'; '.join(out)})")


def finish(result):
    print(f"ls_match_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
