#!/usr/bin/env python3
"""Lift the x86 code inside EA AEMS sound banks (Audio/aems.big, *.abk) ahead of time.

The game copies each bank's resident image (offsets [0, residentsize)) to the heap and calls its
module code through callback lists, so the static lift never sees it (PROGRESS.md 2026-10-03).
Bank layout (header per xan1242/abktool AemsDef_ModuleBank, the rest from the game's loader
0x7C6EB0 and a runtime capture):
  +0x0A nummodules, +0x18 residentsize, +0x30 funcfixup table, +0x34 static-data fixup table,
  modules at 0x5C, 0x40 bytes each: +0x28 code offset, +0x2C data offset.
  fixup table: u32 count, then u32 offsets into the image.
  function fixup: the dword at the offset is an index into the engine's function table
    (0x9AF720 in the exe); the loader turns it into the rel32 of a call to table[index].
  data fixup: the dword is an image offset; the loader adds the load base.

Each bank is lifted at a canonical address AEMS_CANON + i * 0x10000 with function fixups resolved
to the exe functions, and every data-fixup value (an image address the loader rebases) is rewritten
as aems_base[i] + offset, so the C runs against wherever the game put the bank. The runtime
(runtime/core/aems.c) finds a bank's base on the first dispatch miss into it by matching code bytes
(fixups masked) and maps heap addresses to these functions.
Output: gen/aems/aems.c. Prints one line; details -> logs/aems_lift.log."""

import pathlib
import re
import struct
import sys

root = pathlib.Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(root / "tools/lift"))
# lift puts pcrecomp on sys.path, so it is imported first
# isort: off
import lift  # noqa: E402  (pcrecomp paths, lifter setup, carry_flags)
import generate  # noqa: E402
import jumptables  # noqa: E402
# isort: on

AEMS_CANON = 0x7E000000
FUNC_TABLE = 0x9AF720
BIG = root / "Game Files/Audio/aems.big"
OUT = root / "gen/aems/aems.c"


def banks():
    d = BIG.read_bytes()
    n = struct.unpack(">I", d[8:12])[0]
    p, out = 16, []
    for _ in range(n):
        off, size = struct.unpack(">II", d[p : p + 8])
        p += 8
        e = d.index(b"\0", p)
        name = d[p:e].decode()
        p = e + 1
        if name.endswith(".abk") and d[off : off + 4] == b"ABKC":
            out.append((name, d[off : off + size]))
    return out


def rebase(body, i, values, canon):
    """Data-fixup values are runtime addresses: aems_base[i] + offset. Code addresses stay
    canonical (the runtime dispatch accepts both); `case` labels and comments are left alone."""

    def fix(code):
        return re.sub(
            r"0x([0-9A-Fa-f]{6,8})u?\b",
            lambda m: (
                f"(aems_base[{i}] + 0x{int(m.group(1), 16) - canon:X}u)"
                if int(m.group(1), 16) in values
                else m.group(0)
            ),
            code,
        )

    out = []
    for line in body.split("\n"):
        if line.lstrip().startswith("case "):
            out.append(line)
            continue
        k = line.find("/*")
        out.append(fix(line) if k < 0 else fix(line[:k]) + line[k:])
    return "\n".join(out)


def modules(d, nmod, res):
    """(code offset, data offset) per module. Entries start at 0x5C but vary in size (some carry
    extra dwords), so they are found by their marker dword 0x0001010x followed by the pair."""
    out, k = [], 0x5C
    while len(out) < nmod and k + 12 <= res:
        mark, code, data = struct.unpack_from("<III", d, k)
        if mark & 0xFFFFFF00 == 0x00010100 and 0x5C < code < data < res:
            out.append((code, data))
            k += 12
        else:
            k += 4
    if len(out) != nmod:
        raise ValueError(f"found {len(out)} of {nmod} module entries")
    return out


def table(d, off):
    return (
        [struct.unpack_from("<I", d, off + 4 + 4 * k)[0] for k in range(struct.unpack_from("<I", d, off)[0])]
        if off
        else []
    )


def main():
    log = open(root / "logs/aems_lift.log", "w")
    lift._init()
    import pefile

    pe = pefile.PE(lift.EXE, fast_load=True)
    exe = pe.get_memory_mapped_image()
    ib = pe.OPTIONAL_HEADER.ImageBase
    ftab = lambda k: struct.unpack_from("<I", exe, FUNC_TABLE - ib + 4 * k)[0]
    lifter, md = lift._G["lifter"], lift._G["md"]
    c_funcs, c_banks, nfn, nerr = [], [], 0, 0
    for i, (name, d) in enumerate(banks()):
        canon = AEMS_CANON + i * 0x10000
        nmod = struct.unpack_from("<H", d, 0x0A)[0]
        res = struct.unpack_from("<I", d, 0x18)[0]
        ff, sf = table(d, struct.unpack_from("<I", d, 0x30)[0]), table(d, struct.unpack_from("<I", d, 0x34)[0])
        mods = modules(d, nmod, res)
        code_lo, code_hi = min(c for c, _ in mods), min(x for _, x in mods)
        image = bytearray(d[:res])
        for o in ff:  # call rel32 -> engine function table[index]
            tgt = ftab(struct.unpack_from("<i", d, o)[0])
            struct.pack_into("<I", image, o, (tgt - (canon + o + 4)) & 0xFFFFFFFF)
        for o in sf:
            struct.pack_into("<I", image, o, canon + struct.unpack_from("<I", d, o)[0])
        # entries: module code starts, then direct call targets and code pointers in data fixups,
        # to a fixpoint (each body is decoded up to the next entry)
        entries = {canon + c for c, _ in mods}
        entries |= {
            canon + struct.unpack_from("<I", d, o)[0]
            for o in sf
            if code_lo <= struct.unpack_from("<I", d, o)[0] < code_hi
        }
        while True:
            new = set()
            ordered = sorted(entries)
            for k, a in enumerate(ordered):
                end = ordered[k + 1] if k + 1 < len(ordered) else canon + code_hi
                for ins in md.disasm(bytes(image[a - canon : end - canon]), a):
                    if ins.mnemonic == "call" and ins.op_str.startswith("0x"):
                        t = int(ins.op_str, 16)
                        if canon + code_lo <= t < canon + code_hi and t not in entries:
                            new.add(t)
            if not new:
                break
            entries |= new
        ordered = sorted(entries)
        lifter.lifted = set(lift._G["entries"]) | entries
        bank_fns = []
        for k, a in enumerate(ordered):
            end = ordered[k + 1] if k + 1 < len(ordered) else canon + code_hi
            fname = f"sub_{a:08X}"
            try:
                insns, leaders = jumptables.sweep(md, bytes(image), canon, a, end)
                body = generate.lift_function_linear(lifter, fname, insns, leaders, a)
            except Exception as e:  # noqa: BLE001 -- counted, as in lift.py
                body, nerr = f'void {fname}(void) {{ recomp_fault(0x{a:08X}u, "AEMS lift error"); }}\n', nerr + 1
                print(f"{name} {fname}: {e}", file=log)
            body = lift.carry_flags(body)
            body = rebase(body, i, {canon + struct.unpack_from("<I", d, o)[0] for o in sf}, canon)
            c_funcs.append(body)
            bank_fns.append(a)
        nfn += len(bank_fns)
        mask = bytearray(res)
        for o in ff + sf:
            mask[o : o + 4] = b"\1\1\1\1"
        c_banks.append((name, canon, res, code_lo, code_hi, d[:res], mask, bank_fns))
        print(
            f"{name}: canon 0x{canon:08X} res 0x{res:X} code 0x{code_lo:X}-0x{code_hi:X} "
            f"{len(ff)} func + {len(sf)} data fixups, {len(bank_fns)} functions",
            file=log,
        )
    OUT.parent.mkdir(parents=True, exist_ok=True)
    src = [
        "/* Generated by tools/lift/aems_lift.py -- AEMS sound-bank code (do not edit). */",
        "#define RECOMP_GENERATED_CODE",
        '#include "recomp_types.h"',
        '#include "../recomp/recomp_funcs.h"',
        "#include <math.h>",
        "#include <string.h>",
        '#include "aems.h"',
        "",
        f"uint32_t aems_base[{len(c_banks)}];",
        "",
    ]
    for fn in sorted({f"sub_{a:08X}" for b in c_banks for a in b[7]}):
        src.append(f"void {fn}(void);")
    src += c_funcs
    for k, (name, canon, res, lo, hi, raw, mask, fns) in enumerate(c_banks):
        src.append(f"static const uint8_t aems_img_{k}[] = {{{','.join(str(x) for x in raw[lo:hi])}}};")
        src.append(f"static const uint8_t aems_mask_{k}[] = {{{','.join(str(x) for x in mask[lo:hi])}}};")
        src.append(
            f"static const recomp_dispatch_entry_t aems_fns_{k}[] = {{"
            + ",".join(f"{{0x{a:08X}u, sub_{a:08X}}}" for a in fns)
            + "};"
        )
    src.append("const aems_bank_t aems_banks[] = {")
    for k, (name, canon, res, lo, hi, raw, mask, fns) in enumerate(c_banks):
        src.append(
            f'    {{ "{name}", 0x{canon:08X}u, 0x{res:X}u, 0x{lo:X}u, 0x{hi:X}u, aems_img_{k}, aems_mask_{k}, '
            f"aems_fns_{k}, {len(fns)}u }},"
        )
    src += ["};", f"const uint32_t aems_bank_count = {len(c_banks)};", ""]
    OUT.write_text("\n".join(src))
    print(f"aems_lift: {len(c_banks)} banks, {nfn} functions, {nerr} errors -> {OUT.relative_to(root)}")


if __name__ == "__main__":
    main()
