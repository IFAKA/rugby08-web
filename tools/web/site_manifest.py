#!/usr/bin/env python3
"""The party site's folder: the web build's program without the game's files, whose manifest says "own"
(each player brings their own Rugby 08: platform/web/js/own_copy.js).
usage: site_manifest.py [BUILD_DIR] [OUT_DIR]   (default build/web build/site)
OUT_DIR gets every top-level file of BUILD_DIR (links) and data/manifest.json: the files the game reads
(Audio/, Data/) with each 8 MiB chunk's sha256, read from BUILD_DIR/data (tools/web/package_assets.py).
No chunk is copied. Prints one summary line; details in logs/site_manifest.log."""

import hashlib
import json
import os
import shutil
import sys

GAME_DIRS = ("Audio/", "Data/")  # the rest of an install (exe, installer, help pages) differs per copy

src = sys.argv[1] if len(sys.argv) > 1 else "build/web"
out = sys.argv[2] if len(sys.argv) > 2 else "build/site"
os.makedirs("logs", exist_ok=True)
log = open("logs/site_manifest.log", "w")
man = json.load(open(os.path.join(src, "data", "manifest.json")))

files, ids = [], hashlib.sha256()
for f in man["files"]:
    if not f["path"].startswith(GAME_DIRS):
        log.write(f"skip\t{f['path']}\n")
        continue
    for c in f["chunks"]:
        with open(os.path.join(src, "data", c["name"]), "rb") as b:
            c["sha256"] = hashlib.sha256(b.read()).hexdigest()
        ids.update(c["sha256"].encode())
    files.append(f)
    log.write(f"game\t{f['path']}\t{f['size']}\n")

if os.path.isdir(out):
    shutil.rmtree(out)
os.makedirs(os.path.join(out, "data"))
for name in sorted(os.listdir(src)):
    p = os.path.join(src, name)
    if os.path.isfile(p):
        os.symlink(os.path.abspath(p), os.path.join(out, name))
        log.write(f"program\t{name}\n")
site = {"chunk_size": man["chunk_size"], "own": True, "id": ids.hexdigest(), "files": files}
with open(os.path.join(out, "data", "manifest.json"), "w") as f:
    json.dump(site, f, indent=0)
total = sum(f["size"] for f in files)
print(f"site_manifest: {len(files)} game files ({total} bytes, players' own), program -> {out}")
