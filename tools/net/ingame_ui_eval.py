#!/usr/bin/env python3
"""on.ingame_ui evaluation (tools/net/ingame_ui_check.sh): DIR holds host/friend runs and the host's shots
before.png (Online screens closed) and code.png (the room code shown); CODE the room the host was given.
Prints one summary line; exit 0 = a friend joined the host from the in-game screens."""

import os
import re
import sys

from PIL import Image

TEXT = re.compile(r'ms: ui text "(\w+)" (\d+),(\d+),(\d+),(\d+)')


def gold(png, box):
    px = Image.open(png).convert("RGB").crop(box).tobytes()
    return sum(1 for i in range(0, len(px), 3) if px[i] > 200 and 150 < px[i + 1] < 230 and px[i + 2] < 110)


def fail(msg):
    print(f"ingame_ui_check: FAIL ({msg})")
    return 1


def main(d, code):
    host, friend = open(f"{d}/host.out", errors="replace").read(), open(f"{d}/friend.out", errors="replace").read()
    server = open(f"{d}/server.log", errors="replace").read()
    m = TEXT.search(host)
    if not m or m.group(1) != code:
        return fail(f"the host never drew its code {code}")
    x, y, w, h = map(int, m.group(2, 3, 4, 5))
    box = (x, y, x + w, y + h)
    shown, before = gold(f"{d}/code.png", box), gold(f"{d}/before.png", box)
    if shown < 200 or before > shown / 10:
        return fail(f"the code is not visible in the host's picture ({shown} gold pixels, {before} before)")
    web = open(f"{d}/web.log", errors="replace").read() if os.path.exists(f"{d}/web.log") else ""
    joined = re.compile(rf"ms: ui room {code} as player ([234]) \(friend\)")
    nat, brw = joined.search(friend), joined.search(web)
    if not nat:
        return fail(f"the native friend did not join room {code} from its screens")
    if not brw:
        return fail(f"the browser friend did not join room {code} ({open(f'{d}/web.out').read().strip()})")
    if len(re.findall(rf"room {code}: .* joins as player", server)) < 2:
        return fail(f"the relay did not put both friends in room {code}")
    if host.count(f"ms: ui room {code} members 2") < 2:  # one friend after the other
        return fail("the host's screens did not show each friend joining")
    if "joining the host's match" not in friend or not re.search(r"pup dt \S+ worst [0-3] cm", friend):
        return fail("the friend did not follow the host into its match")
    print(
        f"ingame_ui_check: ok (room {code}: drawn in the host's picture ({shown} gold pixels, {before} before), "
        f"a native friend typed it on its Online screens and joined as player {nat.group(1)}, a browser friend as player "
        f"{brw.group(1)}; the native friend then followed the host into its match)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
