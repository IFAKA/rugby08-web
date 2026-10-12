"""The catalog's closure passes (tools/lift/build_catalog.py): targets the protector stubs compute from image
constants (stubemu), then direct calls, pointer-slot jumps and switch arms outside their Ghidra extent."""

import bisect
import re as _re
import struct as _st

from catalog_image import (
    T_HI,
    T_LO,
    base,
    decodes_cleanly,
    g_starts,
    ghidra,
    img,
    in_code,
    in_text,
    inside_ghidra,
    log,
    md,
    pe,
)

# Import address table: its slots hold import RVAs until the loader binds them, never code
_idata = [s for s in pe.sections if s.Name.rstrip(b"\0") == b".idata"][0]
IAT_LO = base + _idata.VirtualAddress
IAT_HI = IAT_LO + _idata.Misc_VirtualSize


def slot_target(op_str):
    """Code address held by the static pointer slot of `dword ptr [0xP]`, else None. The protector's
    stubs (`jmp [0x1380008]` -> 0x13A5C90) reach relocated game functions in either code range."""
    if not (op_str.startswith("dword ptr [0x") and op_str.endswith("]")):
        return None
    p = int(op_str[len("dword ptr [") : -1], 16)
    if IAT_LO <= p < IAT_HI or not base <= p < base + len(img) - 4:
        return None
    v = _st.unpack("<I", img[p - base : p - base + 4])[0]
    return v if in_code(v) else None


import stubemu  # noqa: E402
from ranges import CODE_HI, PROT  # noqa: E402

_emu = stubemu.StubEmu(base, pe.get_memory_mapped_image())  # unpatched image: what the game runs
_SWITCH = _re.compile(r"dword ptr \[e[a-z]{2}\*4 \+ (0x[0-9a-f]+)\]$")


def outside_arms(i):
    """Arms of the switch table behind `jmp [reg*4 + T]` that lie outside the jmp's Ghidra extent.
    Ghidra cut some switch functions short (0x4853A0 ends at 0x485A9E, its arms run to 0x4860D5;
    0x47DFA3 is the bare jmp), so those arms were in no lifted body: the local dispatch found no
    label, RECOMP_ITAIL no function, and the case was skipped. Each one becomes an entry (17 tables,
    181 arms). Slots are read as jumptables.table_arms does: code pointers near the jmp, a short
    biased run before slot 0, stop at the second non-pointer after the first arm."""
    m = _SWITCH.match(i.op_str)
    if not m:
        return set()
    t = int(m.group(1), 16)
    j = bisect.bisect_right(g_starts, i.address) - 1
    g_lo, g_hi = ghidra[j] if j >= 0 and ghidra[j][0] <= i.address < ghidra[j][1] else (i.address, i.address + 1)
    arms, via, misses = set(), set(), 0
    for k in range(256):
        s = t + 4 * k
        if not base <= s < base + len(img) - 4:
            break
        v = _st.unpack("<I", img[s - base : s - base + 4])[0]
        if in_code(v) and abs(v - i.address) < 0x10000:
            arms.add(v)
            misses = 0
            continue
        # the protector points slots at stubs that rebuild the case address from image constants
        # (0x402AAC[1] -> 0x45E637 -> 0x4029A9): the stub and the case it reaches are both entries
        r = _emu.target(v) if in_code(v) else None
        if r is not None and abs(r - i.address) < 0x10000:
            via |= {v, r}
            misses = 0
            continue
        misses += 1
        if (arms and misses >= 2) or (not arms and misses >= 4):
            break
    return {v for v in arms if not g_lo <= v < g_hi} | via


def emulated_targets(entries):
    """Computed jumps only the image's constants decide (tools/lift/stubemu.py): run every entry on the
    static image, take each indirect jmp/call and each ret that pops no return address of its own.
    Normal functions return to the empty stack's 0 and stop; protector stubs reach their targets."""
    emu_new = set()
    todo = set(entries)
    while todo:
        found = set()
        for va in todo:
            found |= {t for t in _emu.transfers(va) if in_code(t) and t not in entries and decodes_cleanly(t)}
        emu_new |= found
        entries |= found
        todo = found

    return len(emu_new)


def call_targets(entries):
    """Direct-call closure; `jmp/call [slot]` targets are resolved here too, decoding from each entry's
    own start (a .text sweep loses alignment on the protector's junk bytes and misses stubs)."""
    # own start (the .text sweep above loses alignment on the protector's junk bytes and misses stubs,
    # and only took targets inside the protector region: 0x45CE10 `jmp [0x1384804]` -> 0x4310A0)
    rounds, added_total = 0, 0
    while True:
        rounds += 1
        order = sorted(entries)
        new = set()
        for k, a in enumerate(order):
            end = min(order[k + 1] if k + 1 < len(order) else CODE_HI, a + 0x10000)
            if a < T_HI <= end:
                end = T_HI
            # an instruction that straddles `end` is decoded whole, as tools/lift/lift.py straddle_end lifts
            # it: a junk-derived seed at 0x4600D2 must not hide the stub `jmp [0x1380008]` at 0x4600D0
            for i in md.disasm(img[a - base : min(end + 16, CODE_HI) - base], a):
                if i.address >= end:
                    break
                if i.mnemonic == "call" and i.op_str.startswith("0x"):
                    t = int(i.op_str, 16)
                    # junk entries call mid-instruction too (0x417F14 inside `mov eax, 0x2B48`)
                    if in_text(t) and t not in entries and decodes_cleanly(t):
                        new.add(t)
                elif i.mnemonic in ("call", "jmp") and (t := slot_target(i.op_str)) is not None:
                    if t not in entries and decodes_cleanly(t):
                        new.add(t)
                elif i.mnemonic == "jmp" and i.op_str.startswith("dword ptr [e"):
                    new |= {t for t in outside_arms(i) if t not in entries and decodes_cleanly(t)}
                elif i.mnemonic.startswith("j") and i.op_str.startswith("0x") and not (a >= PROT[0]):
                    t = int(i.op_str, 16)
                    if PROT[0] <= t < PROT[1] and t not in entries and decodes_cleanly(t):
                        new.add(t)
                elif i.mnemonic in ("push", "mov") and ", 0x" in (", " + i.op_str):
                    t = int(i.op_str.rsplit("0x", 1)[1], 16) if i.op_str.rsplit(" ", 1)[-1].startswith("0x") else 0
                    if T_LO <= t < T_HI and t not in entries and not inside_ghidra(t) and decodes_cleanly(t):
                        new.add(t)
                if i.mnemonic in ("int3",):
                    break
        if not new:
            break
        entries |= new
        added_total += len(new)
        print(f"round {rounds}: +{len(new)} call targets", file=log)

    return added_total
