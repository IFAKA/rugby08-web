#!/usr/bin/env python3
"""Add a WebAssembly "compilationHints" section: functions with a body above LIMIT bytes stay on V8's
baseline compiler (Liftoff). The lifted game has functions of up to 850 KB; V8's optimizing tier
(TurboFan) needs ~1 KB of zone memory per body byte and dies with "V8 process OOM (Zone)" in the
renderer when such a function turns hot. usage: wasm_hints.py FILE.wasm [LIMIT]. One summary line."""
import sys

def leb(b, p):
    r = s = 0
    while True:
        x = b[p]; p += 1; r |= (x & 0x7f) << s; s += 7
        if not x & 0x80: return r, p

def enc(n):
    o = bytearray()
    while True:
        x = n & 0x7f; n >>= 7
        o.append(x | (0x80 if n else 0))
        if not n: return bytes(o)

path = sys.argv[1]; limit = int(sys.argv[2]) if len(sys.argv) > 2 else 50000
b = open(path, 'rb').read()
p = 8; code = None; have = False
while p < len(b):
    sid = b[p]; sz, q = leb(b, p + 1)
    if sid == 0:
        l, q2 = leb(b, q)
        if b[q2:q2 + l] == b'compilationHints': have = True
    if sid == 10: code = (p, q, sz)
    p = q + sz
if have: print('wasm_hints: already present'); sys.exit(0)
start, body, sz = code
n, q = leb(b, body)
hints = bytearray(); big = 0
for _ in range(n):
    s, q2 = leb(b, q); q = q2 + s
    if s > limit: hints.append(0x14 | 0x00); big += 1     # baseline tier = Liftoff (1<<2), top tier = Liftoff (1<<4)
    else: hints.append(0)
payload = b'\x10compilationHints' + enc(n) + bytes(hints)
sec = b'\x00' + enc(len(payload)) + payload
open(path, 'wb').write(b[:start] + sec + b[start:])
print(f'wasm_hints: {big} of {n} functions above {limit} bytes pinned to the baseline compiler')
