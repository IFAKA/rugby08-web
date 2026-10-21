#!/usr/bin/env python3
"""fps.party: a party with a slow player and a 144 Hz player, each at its own rate, the match in step.

Two native games in one lockstep room (relay on a free port, hidden windows, muted), in real time, into a
PLAY NOW match from the netplay replays (tests/replays/net_p1.txt, net_p2.txt). Player 1 has a slow GPU
(RT_TEST_DRAW_MS: every drawn frame costs that much more), player 2 a 144 Hz display (RT_TEST_HZ; its
frames between the steps come from the render thread). Measured over the match's last seconds, from each
game's "perf:" line a second (frame.c):
  slow    60 steps a second (within 2%), fewer than 45 drawn and steady (each second within 2 of the median);
  144 Hz  60 steps a second, at least 120 frames a second shown (steps and frames between them);
  both    the netplay state hashes the same and no DESYNC.
(Two web pages on one Mac cannot show this: they share its CPU, and the web build has no render thread yet.)
One line; details in logs/fps_party/."""

import os
import re
import socket
import statistics
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
D = os.path.join(ROOT, "logs", "fps_party")
FRAMES, SECS, DRAW_MS, HZ = 15000, 25, 25, 144
PERF = re.compile(r"^perf: (\d+) fps, ([\d.]+) steps/s \((\d+) not drawn\).* (\d+) frames between steps")
HASH = re.compile(r"^netplay: frame \d+ hash \w+")


def relay():
    if subprocess.run(
        ["go", "build", "-o", "../build/rugby08-server", "."], cwd=os.path.join(ROOT, "server")
    ).returncode:
        return None, 0
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    srv = subprocess.Popen(
        [os.path.join(ROOT, "build", "rugby08-server"), "-addr", f"127.0.0.1:{port}", "-dir", "build/web"],
        cwd=ROOT,
        stdout=open(os.path.join(D, "server.log"), "w"),
        stderr=subprocess.STDOUT,
    )
    for _ in range(100):  # (until it listens)
        try:
            socket.create_connection(("127.0.0.1", port), timeout=1).close()
            return srv, port
        except OSError:
            time.sleep(0.1)
    srv.kill()
    return None, 0


def game(n, port, room, env):
    e = dict(os.environ, RT_FPS="60", SDL_AUDIODRIVER="dummy", **env)
    cmd = [os.path.join(ROOT, "build", "native", "rugby08"), "--frames", str(FRAMES)]
    cmd += ["--replay", f"tests/replays/net_p{n}.txt", "--log", os.path.join(D, f"p{n}.log")]
    cmd += ["--net", f"ws://127.0.0.1:{port}/ws", "--net-room", room, "--net-delay", "4", "--net-name", f"P{n}"]
    return subprocess.Popen(
        cmd, cwd=ROOT, env=e, stdout=open(os.path.join(D, f"p{n}.out"), "w"), stderr=subprocess.STDOUT
    )


def judge(name, lines, slow, out):
    perf = [tuple(float(x) for x in m.groups()) for x in lines for m in [PERF.search(x)] if m][-SECS:]
    if len(perf) < SECS:
        return f"{name}: only {len(perf)} seconds measured"
    steps = statistics.mean(p[1] for p in perf)
    drawn = [p[0] for p in perf]
    shown = statistics.median(p[0] + p[3] for p in perf)
    med = statistics.median(drawn)
    out.append(
        f"{name}: {steps:.2f} steps/s, {med:.0f} fps drawn ({min(drawn):.0f}-{max(drawn):.0f}), "
        f"{shown:.0f} frames/s shown, {sum(p[2] for p in perf):.0f} steps not drawn"
    )
    if not 58.8 <= steps <= 61.2:
        return f"{name}: {steps:.2f} steps a second, not 60"
    if slow and (med >= 45 or min(drawn) < med - 2 or max(drawn) > med + 2):
        return f"{name}: drew {min(drawn):.0f}-{max(drawn):.0f} fps (median {med:.0f}): not slow and steady"
    if not slow and shown < 120:
        return f"{name}: {shown:.0f} frames a second shown, not ~{HZ}"
    return None


def run(port, out):
    room = f"F{os.getpid()}"
    ps = [game(1, port, room, {"RT_TEST_DRAW_MS": str(DRAW_MS)}), game(2, port, room, {"RT_TEST_HZ": str(HZ)})]
    try:
        codes = [p.wait(timeout=900) for p in ps]
    finally:
        for p in ps:
            p.kill()
    out.append(f"exits {codes[0]}/{codes[1]}")
    lines = [open(os.path.join(D, f"p{n}.out"), errors="replace").read().splitlines() for n in (1, 2)]
    for name, ls, slow in (("slow", lines[0], True), ("144 Hz", lines[1], False)):
        err = judge(name, ls, slow, out)
        if err:
            return err
    hashes = [[x for x in ls if HASH.search(x)] for ls in lines]
    out.append(f"{len(hashes[0])} netplay hashes")
    if any("DESYNC" in x for ls in lines for x in ls):
        return "DESYNC"
    if len(hashes[0]) < FRAMES / 120 - 2 or hashes[0] != hashes[1]:
        return f"the state hashes differ or are missing ({len(hashes[0])}/{len(hashes[1])})"
    return None


def main():
    os.makedirs(D, exist_ok=True)
    srv, port = relay()
    if not srv:
        return finish("FAIL (relay did not build or start)")
    out, err = [], None
    try:
        err = run(port, out)
    except subprocess.TimeoutExpired:
        err = "a game did not finish in 900 s"
    finally:
        srv.kill()
        open(os.path.join(D, "steps.log"), "w").write("\n".join(out) + "\n")
    return finish(f"FAIL ({err}; {'; '.join(out)})" if err else f"ok ({'; '.join(out)})")


def finish(result):
    print(f"fps_party_check: {result}", flush=True)
    return 0 if result.startswith("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
