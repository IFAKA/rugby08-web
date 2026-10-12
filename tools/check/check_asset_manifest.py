#!/usr/bin/env python3
"""Verify manifest.json against the original tree: every file listed, sizes and sha256 match when
chunks are reassembled, no extra entries. usage: check_asset_manifest.py GAME_DIR MANIFEST"""

import hashlib
import json
import os
import sys

src, mpath = sys.argv[1], sys.argv[2]
ddir = os.path.dirname(mpath)
m = json.load(open(mpath))
listed = {e["path"]: e for e in m["files"]}
actual = {os.path.relpath(os.path.join(r, f), src).replace(os.sep, "/") for r, _, fs in os.walk(src) for f in fs}
errs = [f"missing from manifest: {p}" for p in sorted(actual - listed.keys())]
errs += [f"not in Game Files: {p}" for p in sorted(listed.keys() - actual)]
for p in sorted(actual & listed.keys()):
    e, h, size = listed[p], hashlib.sha256(), 0
    for c in e["chunks"]:
        cp = os.path.join(ddir, c["name"])
        if not os.path.isfile(cp) or os.path.getsize(cp) != c["size"]:
            errs.append(f"bad chunk {c['name']} for {p}")
            break
        with open(cp, "rb") as f:
            b = f.read()
            h.update(b)
            size += len(b)
    else:
        if size != e["size"] or size != os.path.getsize(os.path.join(src, p)) or h.hexdigest() != e["sha256"]:
            errs.append(f"content mismatch: {p}")
        else:
            with open(os.path.join(src, p), "rb") as f:
                if hashlib.sha256(f.read()).hexdigest() != e["sha256"]:
                    errs.append(f"sha256 differs from original: {p}")
os.makedirs("logs", exist_ok=True)
open("logs/check_asset_manifest.log", "w").write("\n".join(errs) + "\n")
if errs:
    print(f"check_asset_manifest: FAIL {len(errs)} errors (first: {errs[0]})")
    sys.exit(1)
print(f"check_asset_manifest: ok ({len(listed)} files)")
