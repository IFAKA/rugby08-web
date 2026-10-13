#!/usr/bin/env python3
"""The game's pictures dumped by RT_PIX_DUMP=A-B ("pix: dump F 160 120 HEX", runtime/debug/frames.cpp)
as PNGs: pix_dump.py OUTDIR LOG [LOG2]. With two logs (two players), only the frames whose pictures
differ, each player's and a difference image (white where they differ). One line; PNGs in OUTDIR."""

import os
import re
import struct
import sys
import zlib

DUMP = re.compile(r"pix: dump (\d+) (\d+) (\d+) ([0-9A-F]+)")


def dumps(path):
    start, out = 0, {}
    lines = open(path, errors="replace").read().split("\n")
    start = max((i for i, x in enumerate(lines) if "netplay: session started" in x), default=0)
    for x in lines[start:]:
        m = DUMP.search(x)
        if m:
            out[int(m.group(1))] = (int(m.group(2)), int(m.group(3)), bytes.fromhex(m.group(4)))
    return out


def png(path, w, h, rgb):
    rows = b"".join(b"\0" + rgb[(h - 1 - y) * w * 3 : (h - y) * w * 3] for y in range(h))  # bottom row first

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
    open(path, "wb").write(data)


def main():
    d, logs = sys.argv[1], sys.argv[2:]
    os.makedirs(d, exist_ok=True)
    ds = [dumps(x) for x in logs]
    frames = sorted(set.intersection(*(set(x) for x in ds)))
    if len(ds) == 2:
        frames = [f for f in frames if ds[0][f][2] != ds[1][f][2]]
    for f in frames:
        for i, x in enumerate(ds):
            w, h, rgb = x[f]
            png(os.path.join(d, f"f{f}_{i}.png"), w, h, rgb)
        if len(ds) == 2:
            a, b = ds[0][f][2], ds[1][f][2]
            diff = bytes(255 if abs(a[i] - b[i]) > 8 else 0 for i in range(len(a)))
            png(os.path.join(d, f"f{f}_diff.png"), w, h, diff)
    print(f"pix_dump: {len(frames)} frame(s) written to {d}" + (" (different only)" if len(ds) == 2 else ""))


if __name__ == "__main__":
    sys.exit(main())
