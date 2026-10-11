#!/usr/bin/env python3
"""Build-time patch of third_party/d3d9-webgl/d3d9.cpp (the submodule stays untouched).
Vertex shader constant registers 96..99 (above D3D8's 96) carry the per-player camera correction
(runtime/d3d8/d3d8_vshader.cpp): the upload clamp becomes 100. Usage: patch_d3d9w.py SRC DST"""
import sys
src, dst = sys.argv[1], sys.argv[2]
t = open(src).read()
old = "p->regs[i] < 96 ? p->regs[i] : 0"
if t.count(old) != 2:
    sys.exit(f"patch_d3d9w: expected 2 register clamps, found {t.count(old)}")
t = t.replace(old, "p->regs[i] < 100 ? p->regs[i] : 0")
for a, b in (("float buf[96 * 4];", "float buf[100 * 4];"), ("std::min(p->regs.size(), (size_t)96)", "std::min(p->regs.size(), (size_t)100)")):
    if t.count(a) != 2:
        sys.exit(f"patch_d3d9w: expected 2 of {a!r}, found {t.count(a)}")
    t = t.replace(a, b)
open(dst, "w").write(t)
