"""Lifter-side rewrite of the protection layer's obfuscated calls (the image in guest memory is
NOT changed; only the bytes the lifter decodes).

The shipped .text replaces 933 `call T` instructions with

    push R          68 <R>      R = return address, 4/8/12/16 bytes past the ret
    push T          68 <T>      T = callee
    ret             C3          "returns" into T with R as T's return address
    <junk>                      padding up to R

which a lifter reads as a function return. It is `call T` followed by execution at R, so the
lifter is handed the equivalent  E8 call T; E9 jmp R; 90...  (10 bytes, fits in the 11 + gap).

A second form does the same with a jump instead of the ret (4,010 sites, 72 of them short):

    push R          68 <R>      R = return address, 4/8/12/16 bytes past the jmp
    jmp T           E9 <rel32> | EB <rel8>
    <junk>                      padding up to R

Lifted as written, T's `ret` returns out of the lifted caller and the code at R (usually the
caller's epilog) never runs, leaving esp/ebp wrong. It gets the same call T; jmp R rewrite
(10 bytes, fits in 10 + gap or 7 + gap). REVERSE.md records the census.

When T == R (156 jmp-form sites, 84 push/push/ret-form sites) it is an encrypted call instead: the stub at R computes the callee from
static data (`push ebx; clc; mov ebx,[A]; sbb ebx,[B]; xchg [esp],ebx; add [esp+4],K; ret` and a
dozen arithmetic variants) and returns into it with R+K as its return address. resolve_stub runs
the stub on an emulator over the image bytes; when it reaches its ret with every register but eax
restored, the site becomes call <callee>; jmp R+K (150 sites). The other 6 jump to a dispatcher
whose table is filled at runtime (`add [esp], K; jmp 0xAC54E0`, the key pushed just before the
site): the callee is unknown statically, but the shape is still a call that returns to R+K, so
the site becomes call S; jmp R+K with S = the stub just past its add (dispatch_stub). Left as
written, the generator read `push R; jmp R` as a call returning to R and ran the stub twice.
Their junk gap ends at R, not at the return address (GAP_END).
"""
import re

PAT = re.compile(rb"\x68(....)\x68(....)\xC3", re.S)
PAT_JMP = re.compile(rb"\x68(....)(?:\xE9(....)|\xEB(.))", re.S)
GAPS = (4, 8, 12, 16)
GAP_END = {}    # site -> end of its junk gap, where that is not the return address


def gap_end(site: int, ret: int) -> int:
    return GAP_END.get(site, ret)


def _junk(i) -> bool:
    """Protector filler that changes nothing (flags aside): nop, xchg/mov r,r, lea r,[r], shifts by 0."""
    ops = [o.strip() for o in i.op_str.split(",")]
    if i.mnemonic == "nop":
        return True
    if i.mnemonic in ("xchg", "mov") and len(ops) == 2 and ops[0] == ops[1]:
        return True
    if i.mnemonic == "lea" and len(ops) == 2 and ops[1] == f"[{ops[0]}]":
        return True
    return i.mnemonic in ("shl", "shr", "sar", "rol", "ror") and len(ops) == 2 and ops[1] == "0"


def dispatch_stub(text: bytes, t_lo: int, r: int):
    """(S, R+K) for a stub at r of the form  junk*; add dword ptr [esp], K; junk*; jmp D  (D in code),
    whose callee comes from a runtime-filled dispatcher table; else None."""
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    from ranges import in_code
    o = r - t_lo
    if not 0 <= o < len(text):
        return None
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    s = k = None
    for n, i in enumerate(md.disasm(text[o:o + 48], r)):
        if n >= 8:
            return None
        if s is None:
            if i.mnemonic == "add" and i.op_str.startswith("dword ptr [esp], "):
                k, s = int(i.op_str.rsplit(" ", 1)[1], 0), i.address + i.size
            elif not _junk(i):
                return None
        elif i.mnemonic == "jmp" and i.op_str.startswith("0x"):
            # D (0xAC54E0, 0xAC66C0) is a protector thunk in .data, outside the code ranges
            d = int(i.op_str, 16)
            return (s, r + k) if t_lo <= d < t_lo + len(text) and 0 < k < 0x40 and in_code(r + k) else None
        elif not _junk(i):
            return None
    return None


def find_sites(text: bytes, t_lo: int, t_hi: int, low: bytes = b""):
    """[(site_va, target, ret_va)] for every push R; push T; ret or push R; jmp T with a 4..16-byte junk gap,
    site and targets inside a code range (tools/ranges.py)."""
    from ranges import in_code
    out = []
    def via_thunk(t):
        # 0xAE8780 (in .data, outside the code ranges) is `jmp [0x1159740]` -> the call dispatcher
        # 0x1055000; 5 sites `push KEY; push R; push 0xAE8780; ret` call through it.
        o = t - t_lo
        if not in_code(t) and 0 <= o and o + 6 <= len(text) and text[o:o + 2] == b"\xFF\x25":
            p = int.from_bytes(text[o + 2:o + 6], "little") - t_lo
            if 0 <= p and p + 4 <= len(text):
                return int.from_bytes(text[p:p + 4], "little")
        return t

    for m in PAT.finditer(text):
        r = int.from_bytes(m.group(1), "little")
        t = via_thunk(int.from_bytes(m.group(2), "little"))
        site = t_lo + m.start()
        if in_code(site) and in_code(r) and in_code(t) and (r - (site + 11)) in GAPS:
            if t != r:
                out.append((site, t, r))
            else:                       # encrypted call in the push/push/ret form (84 sites)
                res = resolve_stub(text, t_lo, r, pushed=r, low=low)    # as the site's ret leaves it
                if res and in_code(res[1]) and t_lo <= res[0] < t_lo + len(text):
                    out.append((site, res[0], res[1]))
                elif (d := dispatch_stub(text, t_lo, r)):
                    out.append((site, d[0], d[1])); GAP_END[site] = r
    taken = {a for s, _, r in out for a in range(s, r)}
    for m in PAT_JMP.finditer(text):
        site = t_lo + m.start()
        r = int.from_bytes(m.group(1), "little")
        end = site + (10 if m.group(2) else 7)
        t = (end + int.from_bytes(m.group(2) or m.group(3), "little", signed=True)) & 0xFFFFFFFF
        if site not in taken and in_code(site) and in_code(r) and in_code(t) and (r - end) in GAPS:
            if t != r:
                out.append((site, t, r))
            else:
                res = resolve_stub(text, t_lo, site, low=low)
                if res and in_code(res[1]) and t_lo <= res[0] < t_lo + len(text):
                    out.append((site, res[0], res[1]))
                elif (d := dispatch_stub(text, t_lo, r)):
                    out.append((site, d[0], d[1])); GAP_END[site] = r
    return sorted(out)


_EMU = {}


def resolve_stub(text: bytes, t_lo: int, site: int, steps: int = 2000, pushed=None, low: bytes = b""):
    """Run an encrypted-call site on an emulator over the code buffer -> (callee, return VA) or
    None. Memory the stub writes outside its stack is restored afterwards. pushed: start at the
    stub itself (site) with this return address already on the stack (the push/push/ret form,
    whose own ret would otherwise look like the stub's). low: image bytes just below t_lo (the PE
    header: some stubs take their constants from it). A stub ends at its ret into the callee or,
    in the jump form (`add [esp], K; jmp callee`), on arriving through a jmp with only the return
    address on the stack and every register restored."""
    from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn import x86_const as X
    key = (id(text), t_lo, len(low))
    if _EMU.get("key") != key:
        mu = Uc(UC_ARCH_X86, UC_MODE_32)
        lo = (t_lo - len(low)) & ~0xFFF
        mu.mem_map(lo, (t_lo + len(text) - lo + 0xFFF) & ~0xFFF)
        if low:
            mu.mem_write(t_lo - len(low), bytes(low))
        mu.mem_write(t_lo, bytes(text))
        mu.mem_map(0x100000, 0x10000)        # stack
        _EMU.update(key=key, mu=mu)
    mu = _EMU["mu"]
    regs = [X.UC_X86_REG_EBX, X.UC_X86_REG_ECX, X.UC_X86_REG_EDX, X.UC_X86_REG_ESI,
            X.UC_X86_REG_EDI, X.UC_X86_REG_EBP]
    for k, reg in enumerate(regs):
        mu.reg_write(reg, 0x11110000 * (k + 1) + k)
    top = 0x10F000
    mu.reg_write(X.UC_X86_REG_ESP, top)
    if pushed is not None:
        mu.mem_write(top - 4, (pushed & 0xFFFFFFFF).to_bytes(4, "little"))
        mu.reg_write(X.UC_X86_REG_ESP, top - 4)
    out, saved = {}, {}

    def on_write(uc, access, addr, size, value, _):
        if not 0x100000 <= addr < 0x110000:
            for a in range(addr, addr + size):
                saved.setdefault(a, bytes(uc.mem_read(a, 1)))

    clean = lambda uc: all(uc.reg_read(reg) == 0x11110000 * (k + 1) + k for k, reg in enumerate(regs))
    prev = {"jmp": False}

    def on_code(uc, addr, size, _):
        esp = uc.reg_read(X.UC_X86_REG_ESP)
        if size == 1 and uc.mem_read(addr, 1) == b"\xC3" and esp == top - 8:
            out["callee"] = int.from_bytes(uc.mem_read(top - 8, 4), "little")
            out["ret"] = int.from_bytes(uc.mem_read(top - 4, 4), "little")
            out["clean"] = clean(uc)
            uc.emu_stop()
            return
        ret = int.from_bytes(uc.mem_read(top - 4, 4), "little")
        if pushed is not None and prev["jmp"] and esp == top - 4 and ret != pushed and clean(uc):
            out.update(callee=addr, ret=ret, clean=True)
            uc.emu_stop()
            return
        op = bytes(uc.mem_read(addr, 1))
        prev["jmp"] = op in (b"\xE9", b"\xEB")

    hooks = [mu.hook_add(UC_HOOK_CODE, on_code), mu.hook_add(UC_HOOK_MEM_WRITE, on_write)]
    try:
        mu.emu_start(site, 0, count=steps)
    except UcError:
        out.clear()
    for h in hooks:
        mu.hook_del(h)
    for a, v in saved.items():
        mu.mem_write(a, v)
    return (out["callee"], out["ret"]) if out.get("clean") else None


PAT_PUSHRET = re.compile(rb"\x68(....)\xC3", re.S)


def patch(text: bytes, t_lo: int, t_hi: int, low: bytes = b""):
    """Return (patched bytes, sites). low: image bytes from the image base up to t_lo.
    Bare `push T; ret` left over after the call rewrites (18, in protector code) is `jmp T`, and
    becomes E9 jmp T; 90 -- lifted as written it would return out of the function instead."""
    from ranges import in_code
    buf = bytearray(text)
    sites = find_sites(text, t_lo, t_hi, low)
    for site, t, r in sites:
        o = site - t_lo
        call_rel = (t - (site + 5)) & 0xFFFFFFFF
        jmp_rel = (r - (site + 10)) & 0xFFFFFFFF
        buf[o:o + 5] = b"\xE8" + call_rel.to_bytes(4, "little")
        buf[o + 5:o + 10] = b"\xE9" + jmp_rel.to_bytes(4, "little")
        for k in range(site + 10, gap_end(site, r)):
            buf[k - t_lo] = 0x90
    for m in PAT_PUSHRET.finditer(bytes(buf)):
        site, t = t_lo + m.start(), int.from_bytes(m.group(1), "little")
        if in_code(site) and in_code(t):
            buf[m.start():m.start() + 5] = b"\xE9" + ((t - (site + 5)) & 0xFFFFFFFF).to_bytes(4, "little")
            buf[m.start() + 5] = 0x90
    return bytes(buf), sites
