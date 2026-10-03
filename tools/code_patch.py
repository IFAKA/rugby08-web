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

When T == R (156 sites) it is an encrypted call instead: the stub at R computes the callee from
static data (`push ebx; clc; mov ebx,[A]; sbb ebx,[B]; xchg [esp],ebx; add [esp+4],K; ret` and a
dozen arithmetic variants) and returns into it with R+K as its return address. resolve_stub runs
the stub on an emulator over the image bytes; when it reaches its ret with every register but eax
restored, the site becomes call <callee>; jmp R+K (150 sites). The other 6 jump to a dispatcher
whose table is filled at runtime; they stay as written.
"""
import re

PAT = re.compile(rb"\x68(....)\x68(....)\xC3", re.S)
PAT_JMP = re.compile(rb"\x68(....)(?:\xE9(....)|\xEB(.))", re.S)
GAPS = (4, 8, 12, 16)


def find_sites(text: bytes, t_lo: int, t_hi: int):
    """[(site_va, target, ret_va)] for every push R; push T; ret or push R; jmp T with a 4..16-byte junk gap,
    site and targets inside a code range (tools/ranges.py)."""
    from ranges import in_code
    out = []
    for m in PAT.finditer(text):
        r = int.from_bytes(m.group(1), "little")
        t = int.from_bytes(m.group(2), "little")
        site = t_lo + m.start()
        if in_code(site) and in_code(r) and in_code(t) and (r - (site + 11)) in GAPS:
            out.append((site, t, r))
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
                res = resolve_stub(text, t_lo, site)
                if res and in_code(res[1]) and t_lo <= res[0] < t_lo + len(text):
                    out.append((site, res[0], res[1]))
    return sorted(out)


_EMU = {}


def resolve_stub(text: bytes, t_lo: int, site: int, steps: int = 2000):
    """Run an encrypted-call site on an emulator over the code buffer -> (callee, return VA) or
    None. Memory the stub writes outside its stack is restored afterwards."""
    from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    from unicorn import x86_const as X
    key = (id(text), t_lo)
    if _EMU.get("key") != key:
        mu = Uc(UC_ARCH_X86, UC_MODE_32)
        lo = t_lo & ~0xFFF
        mu.mem_map(lo, (t_lo + len(text) - lo + 0xFFF) & ~0xFFF)
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
    out, saved = {}, {}

    def on_write(uc, access, addr, size, value, _):
        if not 0x100000 <= addr < 0x110000:
            for a in range(addr, addr + size):
                saved.setdefault(a, bytes(uc.mem_read(a, 1)))

    def on_code(uc, addr, size, _):
        if size == 1 and uc.mem_read(addr, 1) == b"\xC3" and uc.reg_read(X.UC_X86_REG_ESP) == top - 8:
            out["callee"] = int.from_bytes(uc.mem_read(top - 8, 4), "little")
            out["ret"] = int.from_bytes(uc.mem_read(top - 4, 4), "little")
            out["clean"] = all(uc.reg_read(reg) == 0x11110000 * (k + 1) + k for k, reg in enumerate(regs))
            uc.emu_stop()

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


def patch(text: bytes, t_lo: int, t_hi: int):
    """Return (patched bytes, sites)."""
    buf = bytearray(text)
    sites = find_sites(text, t_lo, t_hi)
    for site, t, r in sites:
        o = site - t_lo
        call_rel = (t - (site + 5)) & 0xFFFFFFFF
        jmp_rel = (r - (site + 10)) & 0xFFFFFFFF
        buf[o:o + 5] = b"\xE8" + call_rel.to_bytes(4, "little")
        buf[o + 5:o + 10] = b"\xE9" + jmp_rel.to_bytes(4, "little")
        for k in range(site + 10, r):
            buf[k - t_lo] = 0x90
    return bytes(buf), sites
