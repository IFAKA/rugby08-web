"""Lifter-side rewrite of the protection layer's obfuscated calls (the image in guest memory is
NOT changed; only the bytes the lifter decodes).

The shipped .text replaces 933 `call T` instructions with

    push R          68 <R>      R = return address, 4/8/12/16 bytes past the ret
    push T          68 <T>      T = callee
    ret             C3          "returns" into T with R as T's return address
    <junk>                      padding up to R

which a lifter reads as a function return. It is `call T` followed by execution at R, so the
lifter is handed the equivalent  E8 call T; E9 jmp R; 90...  (10 bytes, fits in the 11 + gap).
REVERSE.md records the census.
"""
import re

PAT = re.compile(rb"\x68(....)\x68(....)\xC3", re.S)


def find_sites(text: bytes, t_lo: int, t_hi: int):
    """[(site_va, target, ret_va)] for every push R; push T; ret with a 4..16-byte junk gap."""
    out = []
    for m in PAT.finditer(text):
        r = int.from_bytes(m.group(1), "little")
        t = int.from_bytes(m.group(2), "little")
        site = t_lo + m.start()
        if t_lo <= r < t_hi and t_lo <= t < t_hi and (r - (site + 11)) in (4, 8, 12, 16):
            out.append((site, t, r))
    return out


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
