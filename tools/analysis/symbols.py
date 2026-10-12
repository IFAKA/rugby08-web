#!/usr/bin/env python3
"""Readable names for lifted functions: every slot of every RTTI vtable (analysis/vtables.json) becomes
Namespace::Class::vfN at the address the slot holds. A function shared by several vtables (inherited,
not overridden) keeps the name of the class that declares it (the vtable with the fewest slots).

Output: analysis/symbols.json {"0x0049A1F0": "Hbs::Rugby::TeamStates::FreeState::vf2", ...}
(derived from the exe: kept in analysis/, never committed). Used by tools/query/fn.sh.
  tools/analysis/symbols.py     prints one line; details in logs/symbols.log"""

import json
import pathlib
import struct

root = pathlib.Path(__file__).resolve().parent.parent.parent
IMAGE_BASE, IMAGE_END = 0x400000, 0x1465000


def demangle(rtti):
    """RTTI scope list 'Leaf@Outer@Ns' (innermost first) -> 'Ns::Outer::Leaf'; templates kept raw."""
    return "::".join(reversed([p for p in rtti.split("@") if p]))


def main():
    exe = (root / "Rugby08_unpacked.exe").read_bytes()  # a flat dump: file offset = VA - ImageBase
    vtables = json.loads((root / "analysis/vtables.json").read_text())
    best = {}  # va -> (slots of the declaring vtable, name)
    for vt in vtables:
        base, n, cls = int(vt["vtable"], 16), vt["slots"], demangle(vt["class"])
        for i in range(n):
            off = base - IMAGE_BASE + 4 * i
            if off + 4 > len(exe):
                break
            (va,) = struct.unpack_from("<I", exe, off)
            if not IMAGE_BASE <= va < IMAGE_END:
                continue
            name = f"{cls}::vf{i}"
            if va not in best or n < best[va][0]:
                best[va] = (n, name)
    out = {f"0x{va:08X}": name for va, (_, name) in sorted(best.items())}
    (root / "analysis/symbols.json").write_text(json.dumps(out, indent=0))
    log = root / "logs/symbols.log"
    log.parent.mkdir(exist_ok=True)
    log.write_text("\n".join(f"{k} {v}" for k, v in out.items()) + "\n")
    print(f"symbols: {len(out)} functions named from {len(vtables)} vtables -> analysis/symbols.json")


if __name__ == "__main__":
    main()
