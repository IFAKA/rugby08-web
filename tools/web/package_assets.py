#!/usr/bin/env python3
"""Package every file under 'Game Files' into 8 MiB chunks in build/web/data/ plus manifest.json.
usage: package_assets.py [GAME_DIR] [OUT_DIR]. Prints one summary line; details in logs/package_assets.log."""

import hashlib
import json
import os
import sys

CHUNK = 8 << 20
src = sys.argv[1] if len(sys.argv) > 1 else "Game Files"
out = sys.argv[2] if len(sys.argv) > 2 else "build/web/data"
os.makedirs(out, exist_ok=True)
os.makedirs("logs", exist_ok=True)

files, log = [], open("logs/package_assets.log", "w")
paths = sorted(os.path.join(r, f) for r, _, fs in os.walk(src) for f in fs)
total = 0
for fid, p in enumerate(paths):
    rel = os.path.relpath(p, src).replace(os.sep, "/")
    h, size, chunks = hashlib.sha256(), 0, []
    with open(p, "rb") as f:
        n = 0
        while True:
            b = f.read(CHUNK)
            if not b and n:
                break
            name = f"{fid:05d}.{n:04d}.bin"
            with open(os.path.join(out, name), "wb") as o:
                o.write(b)
            h.update(b)
            size += len(b)
            chunks.append({"name": name, "size": len(b)})
            n += 1
            if not b:
                break
    files.append({"path": rel, "size": size, "sha256": h.hexdigest(), "chunks": chunks})
    total += size
    log.write(f"{rel}\t{size}\t{len(chunks)}\n")
with open(os.path.join(out, "manifest.json"), "w") as f:
    json.dump({"chunk_size": CHUNK, "files": files}, f, indent=0)
print(f"package_assets: {len(files)} files, {total} bytes -> {out}")
