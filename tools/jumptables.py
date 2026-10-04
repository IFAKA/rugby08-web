"""Jump-table-aware linear sweep for tools/lift.py (wraps pcrecomp generate.linear_disassemble_function).

MSVC puts `jmp [reg*4 + T]` tables inside the body when the arms come after them (the CRT's
memcpy/memmove do this several times). The linear sweep decodes the table bytes as instructions,
comes out misaligned, and an arm such as memmove's 0x8720D4 never gets an instruction or a label:
the local dispatch misses it, RECOMP_ITAIL finds no function there, and the body returns to its
caller with esp/ebp/esi half restored (native.boot, 2026-10-03).

So: read each table (as disasm32.jump_table_targets does, but only arms inside the body), treat its
slots as data, make every arm a leader, and decode from each arm that is not already an instruction
start until the decode meets one (resynchronised) or the block ends.
"""
from capstone.x86 import X86_OP_MEM
import generate

MAX_SLOTS = 256


def table_arms(code, cs, i, lo, hi):
    """(slot_lo, slot_hi, arms) of the table behind indirect jmp i, arms inside [lo, hi); or None."""
    if not i.operands:
        return None
    op = i.operands[0]
    if op.type != X86_OP_MEM or op.mem.scale != 4 or not op.mem.index or op.mem.base:
        return None
    t = op.mem.disp & 0xFFFFFFFF
    arms, first, last, misses = [], None, None, 0
    for k in range(MAX_SLOTS):
        a = t + 4 * k
        if a - cs < 0 or a + 4 - cs > len(code) or any(t <= x <= a for x in arms):
            break                               # a table never runs into an arm placed after it
        v = int.from_bytes(code[a - cs:a - cs + 4], "little")
        if lo <= v < hi:
            arms.append(v)
            first = k if first is None else first
            last, misses = k, 0
            continue
        misses += 1                             # biased index / padding before slot 0
        if (arms and misses >= 2) or (not arms and misses >= 4):
            break
    if not arms:
        return None
    # memmove's "unwind down" tables are indexed by a negated count (`neg ecx; jmp [ecx*4 + T]`),
    # so their arms sit in the slots before T (0x872548: slots -7..-1 -> 0x87254C..0x87257C).
    if first == 0:
        while first > -MAX_SLOTS and t + 4 * (first - 1) - cs >= 0:
            a = t + 4 * (first - 1)
            v = int.from_bytes(code[a - cs:a - cs + 4], "little")
            if not (lo <= v < hi) or any(a <= x < t for x in arms + [v]):
                break
            arms.append(v)
            first -= 1
    return t + 4 * first, t + 4 * (last + 1), set(arms)


def sweep(md, code, cs, lo, hi, arms=None):
    """generate.linear_disassemble_function, plus, when the body dispatches through a jump table,
    a recursive descent from the entry over branches, fallthroughs and table arms. The descent's
    instructions win; linear-sweep instructions are kept only where they overlap neither those
    nor a table (code reached some other way, e.g. EH funclets). -> (instructions, leaders)"""
    insns, leaders = generate.linear_disassemble_function(md, code, cs, lo, hi)
    if not any(i.is_uncond_jump and i.get_branch_target() is None for i in insns):
        return insns, leaders
    good, data, work = {}, [], [lo]
    in_data = lambda a: any(d0 <= a < d1 for d0, d1 in data)
    while work:
        a = work.pop()
        if a in good or not (lo <= a < hi) or in_data(a):
            continue
        for ci in md.disasm(code[a - cs:hi - cs], a):
            if ci.address in good or in_data(ci.address):
                break
            li = generate.LinearInstruction(ci)
            good[li.address] = li
            if li.is_jump:
                tg = li.get_branch_target()
                if tg is not None:
                    if lo <= tg < hi:
                        leaders.add(tg)
                        work.append(tg)
                elif li.is_uncond_jump:
                    r = table_arms(code, cs, li, lo, hi)
                    if r:
                        data.append((r[0], r[1]))
                        leaders |= r[2]
                        work.extend(r[2])
                        if arms is not None:        # tools/lift.py reachable(): table-only labels
                            arms |= r[2]
                leaders.add(li.end_address)
                if li.is_uncond_jump:
                    break
            if li.is_ret or li.mnemonic == "int3":
                break
    if not data:
        return insns, leaders
    covered = set()
    for i in good.values():
        covered.update(range(i.address, i.end_address))
    for i in insns:
        if i.address not in good and not in_data(i.address) and \
                not any(b in covered for b in range(i.address, i.end_address)):
            good[i.address] = i
    out = sorted(good.values(), key=lambda x: x.address)
    # a linear-sweep instruction can still overlap a table that a later descent step found
    out = [i for i in out if not any(i.address < d1 and d0 < i.end_address for d0, d1 in data)]
    return out, leaders
