#!/usr/bin/env python3
"""The game's call graph, read from the lifted C (gen/recomp), so execution paths can be studied without
running the game.

  tools/query/callgraph.py callees VA|NAME [--depth N]   what VA calls (a tree, N levels, default 1)
  tools/query/callgraph.py callers VA|NAME [--depth N]   who calls VA
  tools/query/callgraph.py path A B [--max-depth N]      a shortest call chain from A to B
  tools/query/callgraph.py reach A B                     whether B is reachable from A (and how far)

Edges: "call" (call sub_X), "tail" (jmp [slot]: the protector splits functions into pieces joined
through slots in .data; the slot's value is read from Rugby08_unpacked.exe), "icall" (call [K], K a
static slot or import), "vcall +0xNN" (a virtual call through slot NN of an unknown object: listed,
not followed). The graph is cached in analysis/callgraph.json and rebuilt when gen/recomp is newer.
Names come from analysis/symbols.json (tools/analysis/symbols.py)."""

import json
import os
import re
import struct
import sys
from collections import deque

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
GEN = os.path.join(ROOT, "gen", "recomp")
CACHE = os.path.join(ROOT, "analysis", "callgraph.json")
SYM = os.path.join(ROOT, "analysis", "symbols.json")
EXE = os.path.join(ROOT, "Rugby08_unpacked.exe")

HEAD = re.compile(r"^(?:static )?void sub_([0-9A-F]{8})(?:_eh|_lifted)?\(void\) \{")
CALL = re.compile(r"RECOMP_CALL\(sub_([0-9A-F]{8})\)")
SLOT_TAIL = re.compile(r"_itail_tgt = \(uint32_t\)\(MEM32\(0x([0-9A-Fa-f]+)\)\);")
SLOT_CALL = re.compile(r"RECOMP_ICALL\(MEM32\(0x([0-9A-Fa-f]+)\)\)")
VCALL = re.compile(r"RECOMP_ICALL\(MEM32\((?:e[a-d]x|e[sd]i)(?: \+ 0x([0-9A-Fa-f]+))?\)\)")


class Image:
    """Static words of the unpacked exe (slot values), and the import names of IAT slots."""

    def __init__(self):
        import pefile

        self.pe = pefile.PE(EXE, fast_load=True)
        self.pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.imports = {}
        for d in getattr(self.pe, "DIRECTORY_ENTRY_IMPORT", []):
            for i in d.imports:
                if i.name:
                    self.imports[i.address] = i.name.decode()

    def word(self, va):
        try:
            return struct.unpack("<I", self.pe.get_data(va - self.base, 4))[0]
        except Exception:
            return None


def build():
    img = Image()
    graph = {}
    for fn in sorted(os.listdir(GEN)):
        if not re.match(r"recomp_\d+\.c$", fn):
            continue
        cur = None
        for line in open(os.path.join(GEN, fn), errors="replace"):
            m = HEAD.match(line)
            if m:
                cur = graph.setdefault(m.group(1), {})
                continue
            if cur is None:
                continue
            if line.startswith("}"):
                cur = None
                continue
            for m in CALL.finditer(line):
                cur.setdefault(m.group(1), "call")
            for pat, kind in ((SLOT_TAIL, "tail"), (SLOT_CALL, "icall")):
                for m in pat.finditer(line):
                    slot = int(m.group(1), 16)
                    if slot in img.imports:
                        cur.setdefault("import:" + img.imports[slot], kind)
                        continue
                    t = img.word(slot)
                    if t:
                        cur.setdefault(f"{t:08X}", kind)
            for m in VCALL.finditer(line):
                cur.setdefault(f"vcall+0x{int(m.group(1) or '0', 16):X}", "vcall")
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    with open(CACHE, "w") as f:
        json.dump(graph, f)
    return graph


def load():
    newest = max(os.path.getmtime(os.path.join(GEN, f)) for f in os.listdir(GEN))
    if os.path.exists(CACHE) and os.path.getmtime(CACHE) >= newest:
        return json.load(open(CACHE))
    return build()


def names():
    if not os.path.exists(SYM):
        return {}
    return {int(k, 16): v for k, v in json.load(open(SYM)).items()}


def resolve(q, nm):
    if re.fullmatch(r"(0x)?[0-9A-Fa-f]{6,8}", q):
        return f"{int(q, 16):08X}"
    hits = [va for va, n in nm.items() if n == q] or [va for va, n in nm.items() if q.lower() in n.lower()]
    if len(hits) != 1:
        sys.exit(f"callgraph: {len(hits)} functions named like '{q}'" + ("" if not hits else ": be exact"))
    return f"{hits[0]:08X}"


def label(node, nm):
    if not re.fullmatch(r"[0-9A-F]{8}", node):
        return node
    n = nm.get(int(node, 16))
    return f"0x{node}" + (f" {n}" if n else "")


def tree(graph, start, depth, nm, reverse=False):
    edges = graph
    if reverse:
        edges = {}
        for a, outs in graph.items():
            for b, kind in outs.items():
                edges.setdefault(b, {})[a] = kind
    seen = set()

    def walk(node, d, indent):
        for b, kind in sorted(edges.get(node, {}).items()):
            again = b in seen
            print(f"{'  ' * indent}{kind:5} {label(b, nm)}{' ...' if again and edges.get(b) else ''}")
            if not again and d > 1 and b in edges:
                seen.add(b)
                walk(b, d - 1, indent + 1)

    print(label(start, nm) + (" <- callers" if reverse else ""))
    seen.add(start)
    walk(start, depth, 1)


def shortest(graph, a, b, max_depth):
    prev, q = {a: None}, deque([(a, 0)])
    while q:
        node, d = q.popleft()
        if node == b:
            chain = []
            while node is not None:
                chain.append(node)
                node = prev[node]
            return chain[::-1]
        if d >= max_depth:
            continue
        for nxt in graph.get(node, {}):
            if nxt not in prev:
                prev[nxt] = node
                q.append((nxt, d + 1))
    return None


def main(argv):
    if len(argv) < 2 or argv[0] not in ("callees", "callers", "path", "reach"):
        sys.exit(__doc__)
    opts = {"--depth": 1, "--max-depth": 40}
    args = []
    i = 1
    while i < len(argv):
        if argv[i] in opts:
            opts[argv[i]] = int(argv[i + 1])
            i += 2
        else:
            args.append(argv[i])
            i += 1
    graph, nm = load(), names()
    a = resolve(args[0], nm)
    if argv[0] in ("callees", "callers"):
        tree(graph, a, opts["--depth"], nm, reverse=argv[0] == "callers")
        return 0
    b = resolve(args[1], nm)
    chain = shortest(graph, a, b, opts["--max-depth"])
    if not chain:
        print(f"callgraph: no static path from {label(a, nm)} to {label(b, nm)} (virtual calls are not followed)")
        return 1
    if argv[0] == "reach":
        print(f"callgraph: {label(b, nm)} is {len(chain) - 1} calls from {label(a, nm)}")
        return 0
    for k, node in enumerate(chain):
        kind = graph.get(chain[k - 1], {}).get(node, "") if k else ""
        print(f"{'  ' * k}{kind + ' ' if kind else ''}{label(node, nm)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
