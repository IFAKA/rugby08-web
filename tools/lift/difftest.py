#!/usr/bin/env python3
r"""Differential test of the lift on the game's own code: sampled leaf functions run twice from
identical state -- once in Unicorn (the x86 reference), once as our lifted C (tools/lift/lift.py +
lift_ext, compiled with RECOMP_LOCAL_REGS like the real build) -- and every GPR, the x87 depth,
st(0), and hashes of .data, a scratch heap, the stack and every page Unicorn touched are compared.

Port of pcrecomp tools/lift/difftest.py's idea (which tests hand-written snippets) to whole
functions of this binary. Leaf = lifted body with no call/icall/tail/fault/cpuid/rdtsc; bodies
using transcendental x87 ops or 80-bit memory operands are skipped (the model is double
precision; libm vs softfloat differ in the last ulp) -- both by design, see runtime/DESIGN.md.
A case is EXCLUDED (not failed) when the reference itself faults (#DE, bad opcode) or runs away.

  tools/lift/difftest.py --sample 500      exit 0 iff >= 500 compared and 0 mismatches
  tools/lift/difftest.py --sample 20 --grep '_mm\['   only bodies using MMX
  tools/lift/difftest.py --interp --sample 500       also interpreted (runtime/core/interp.c) and handed off
Prints one line; details -> logs/difftest.log.
"""

import argparse
import json
import os
import random
import struct
import subprocess
import sys

from dt_build import SCR_LO, SCR_SZ, STK_LO, B, build_harness, choose, lift, memory_images, root
from dt_ref import compare, run_reference


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sample", type=int, default=500)
    ap.add_argument("--seed", type=int, default=8)
    ap.add_argument("--dump", help="sub_XXXXXXXX: write both machines' final scratch+stack and diff them")
    ap.add_argument("--grep", help="only bodies whose lifted C matches this regex (e.g. '_mm\\[' for MMX)")
    ap.add_argument(
        "--interp",
        action="store_true",
        help="also run every case through runtime/core/interp.c: the whole body interpreted, and "
        "an interpreted call that hands off to the lifted body (difftest_harness.c DT_MODE)",
    )
    a = ap.parse_args()
    B.mkdir(parents=True, exist_ok=True)
    log = open(root / "logs/difftest.log", "w")
    lift._init()
    E = lift._G["entries"]
    rng = random.Random(a.seed)
    chosen, bodies = choose(a, rng)
    exe = build_harness(a, E, chosen, bodies, log)
    if exe is None:
        return 1
    img, scr = memory_images(rng)
    cases, ref, excluded = run_reference(a, E, chosen, bodies, img, scr, rng)
    (B / "cases.txt").write_text("\n".join(cases) + "\n")
    modes = [("native", "0")] + ([("interp", "1"), ("thunk", "2")] if a.interp else [])
    runs = {}
    for mname, mval in modes:
        runs[mname] = subprocess.run(
            [str(exe), str(B / "image.bin"), str(B / "scratch.bin"), str(B / "cases.txt")],
            capture_output=True,
            text=True,
            timeout=3600,
            env={**os.environ, "DT_MODE": mval},
        )
    if a.dump and (B / "dump_c.bin").exists() and (B / "dump_uc.bin").exists():
        c, u = (B / "dump_c.bin").read_bytes(), (B / "dump_uc.bin").read_bytes()
        for o in range(0, len(c), 4):
            if c[o : o + 4] != u[o : o + 4]:
                va = (SCR_LO + o) if o < SCR_SZ else (STK_LO + o - SCR_SZ)
                gc, gu = struct.unpack("<I", c[o : o + 4])[0], struct.unpack("<I", u[o : o + 4])[0]
                print(
                    f"dump {va:08X}: lifted {gc:08X} ({struct.unpack('<f', c[o : o + 4])[0]:.9g})  "
                    f"ref {gu:08X} ({struct.unpack('<f', u[o : o + 4])[0]:.9g})"
                )
    results = {
        mname: compare(runs[mname].stdout, ref, E, chosen, excluded if mname == "native" else {}, mname)
        for mname, _ in modes
    }
    for mname, (compared, mism, fails) in results.items():
        for x in fails:
            print(x, file=log)
    print(
        json.dumps(
            {
                "compared": {m: c for m, (c, _, _) in results.items()},
                "mismatches": {m: x for m, (_, x, _) in results.items()},
                "excluded": excluded,
                "candidates": len(chosen),
            }
        ),
        file=log,
    )
    ok = all(c >= a.sample and x == 0 for c, x, _ in results.values())
    if a.interp:
        print(
            "difftest --interp: "
            + " ".join(f"{m} {c}/{x}" for m, (c, x, _) in results.items())
            + f" (compared/mismatches) excluded={sum(excluded.values())} -> {'PASS' if ok else 'FAIL'}"
        )
    else:
        compared, mism, _ = results["native"]
        print(
            f"difftest: compared={compared} mismatches={mism} excluded={sum(excluded.values())} -> {'PASS' if ok else 'FAIL'}"
        )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
