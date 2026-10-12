#!/usr/bin/env python3
"""Build the lift catalog (analysis/lift_catalog.json): the function entries to lift.

  seeds  = Ghidra starts (analysis/ghidra_funcs.csv, in .text)
         + RTTI virtual methods (analysis/rtti_seeds.json)
         + runtime indirect-call misses (analysis/icall_seeds.json, if present)
         + pcrecomp disasm32 starts that lie OUTSIDE every Ghidra function ("invented") and
           decode cleanly to a ret/jmp (real code Ghidra skipped; data decodes to junk fast)
  closure: linear-decode every entry up to the next entry; add every direct `call` target in
           .text; repeat to a fixpoint.
disasm32 starts that land INSIDE a Ghidra function ("split") are deliberately excluded: making
them entries would cut a real function into pieces. Prints one line; details -> logs/catalog.log.
"""

import json

from catalog_closure import call_targets, emulated_targets
from catalog_image import (  # the image, ranges and predicates every seed source uses
    CS_ARCH_X86,
    CS_MODE_32,
    OBF,
    T_HI,
    T_LO,
    A,
    Cs,
    base,
    code_patch,
    decodes_cleanly,
    g_starts,
    img,
    in_code,
    in_text,
    inside_ghidra,
    log,
    md,
    pe,
)
from ranges import CODE_RANGES, INITTERM, PROT

entries = set(g_starts)
src = {"ghidra": len(entries)}
rtti = {r["address"] for r in json.load(open(A / "rtti_seeds.json")) if in_text(r["address"])}
src["rtti_new"] = len(rtti - entries)
entries |= rtti
obf = {tgt for _, tgt, _ in OBF}
src["obf_call_targets_new"] = len(obf - entries)
entries |= obf
static = A / "static_seeds.json"  # tools/lift/lift_fixpoint.sh: constant tail/call targets
if static.exists():
    st = {a for a in json.load(open(static)) if in_text(a)}
    src["static_new"] = len(st - entries)
    entries |= st
icall = A / "icall_seeds.json"
if icall.exists():
    ic = {int(x, 0) if isinstance(x, str) else x for x in json.load(open(icall))}
    ic = {a for a in ic if in_text(a)}
    src["icall_new"] = len(ic - entries)
    entries |= ic
# Pointers the program itself holds to code: every 4-aligned dword of .data that lands in .text
# (EH FuncInfo/unwind tables, __xc/__xi initializer tables, callback tables).
data = [s for s in pe.sections if s.Name.rstrip(b"\0") == b".data"][0]
D_LO = base + data.VirtualAddress
dbytes = img[D_LO - base : D_LO - base + data.Misc_VirtualSize]
datarefs = {int.from_bytes(dbytes[o : o + 4], "little") for o in range(0, len(dbytes) - 3, 4)}
datarefs = {v for v in datarefs if in_text(v)}

# disasm32 starts outside every Ghidra function are real code only when something points at
# them; decoding cleanly alone let data-decoded-as-code in, and its "call targets" then cut
# real functions apart (sub_00401000 split at 0x401007).
# Protector region (REVERSE.md): game functions reached via `jmp/call [ptr]` stubs whose pointer
# holds the target, and via direct call/jmp/jcc from .text (the counter triggers).
import struct as _st

gend_set = set(g_starts)
prot_seeds = set()
_mds = Cs(CS_ARCH_X86, CS_MODE_32)
_mds.skipdata = True  # a linear sweep of all .text: step over bytes that do not decode
for i in _mds.disasm(img[T_LO - base : T_HI - base], T_LO):
    if i.mnemonic in ("jmp", "call") and i.op_str.startswith("dword ptr [0x") and i.op_str.endswith("]"):
        try:
            p = int(i.op_str[len("dword ptr [") : -1], 16)
        except ValueError:
            continue
        if base <= p < base + len(img) - 4:
            v = _st.unpack("<I", img[p - base : p - base + 4])[0]
            if PROT[0] <= v < PROT[1]:
                prot_seeds.add(v)
    elif (i.mnemonic in ("call", "jmp") or i.mnemonic.startswith("j")) and i.op_str.startswith("0x"):
        v = int(i.op_str, 16)
        # only from real code: an instruction inside a Ghidra function (the sweep also decodes data)
        if PROT[0] <= v < PROT[1] and (inside_ghidra(i.address) or i.address in gend_set):
            prot_seeds.add(v)
prot_seeds = {v for v in prot_seeds if decodes_cleanly(v)}
src["protector_seeds_new"] = len(prot_seeds - entries)
entries |= prot_seeds

# The protector's call dispatcher (0x1055000): `call $+5; pop ebx; add ebx, imm8; xor ecx, ecx;
# cmp [ecx+ebx], eax` finds its own table, a static run of (key, target) pairs; the key is the
# stack word above the return address and the dispatcher jumps to target. Every target is
# reached only through `jmp [esp-0x28]`, so each one is an entry.
import re as _re

disp_seeds, disp_tables = set(), 0
for mt in _re.finditer(rb"\xE8\x00\x00\x00\x00\x5B\x83\xC3(.)\x33\xC9\x39\x04\x19", img, _re.S):
    va = base + mt.start()
    if not in_code(va):
        continue
    t = va + 5 + mt.group(1)[0]
    disp_tables += 1
    while t + 8 - base <= len(img):
        k, v = _st.unpack("<II", img[t - base : t - base + 8])
        if not (in_code(k) and in_code(v)):
            break
        disp_seeds.add(v)
        t += 8
src["dispatcher_tables"] = disp_tables
src["dispatcher_targets_new"] = len(disp_seeds - entries)
entries |= disp_seeds

# every _initterm slot (tools/lift/ranges.py INITTERM): small initializers nothing else references
init = {_st.unpack("<I", img[a - base : a - base + 4])[0] for lo_, hi_ in INITTERM for a in range(lo_, hi_, 4)}
init = {v for v in init if in_code(v)}
src["initterm_new"] = len(init - entries)
entries |= init


# C++ exception handling (runtime/win32/k32_eh.c). MSVC 7.1 FuncInfo (magic 0x1993052x): the CRT's
# __CxxFrameHandler calls the unwind actions and catch handlers through `call eax` (_CallSettingFrame),
# and a catch handler returns its continuation in eax (`mov eax, offset $L; ret`), which
# _JumpToContinuation jumps to. All of them, and every handler a frame installs at fs:[0], are entries.
def _u32(va):
    return _st.unpack("<I", img[va - base : va - base + 4])[0] if base <= va <= base + len(img) - 4 else None


eh, eh_cont = set(), set()
for mt in _re.finditer(rb"[\x20-\x22]\x05\x93\x19", img):
    if mt.start() % 4:
        continue
    fi = base + mt.start()
    max_state, p_unwind, n_try, p_try = (_u32(fi + 4), _u32(fi + 8), _u32(fi + 12), _u32(fi + 16))
    if (
        max_state is None
        or n_try is None
        or max_state > 5000
        or n_try > 500
        or (max_state and _u32(p_unwind) is None)
        or (n_try and _u32(p_try) is None)
    ):
        continue
    acts = [_u32(p_unwind + 8 * s + 4) for s in range(max_state)]
    handlers, good = [], all(a is not None and (a == 0 or in_code(a)) for a in acts)
    for t in range(n_try if good else 0):
        n_catch, p_h = _u32(p_try + 20 * t + 12), _u32(p_try + 20 * t + 16)
        if n_catch is None or n_catch > 64 or _u32(p_h) is None:
            good = False
            break
        handlers += [_u32(p_h + 16 * c + 12) for c in range(n_catch)]
    if not good or not all(h is not None and in_code(h) for h in handlers):
        continue
    eh |= {a for a in acts if a} | set(handlers)
    for h in handlers:  # the continuation the catch block returns
        last = None
        for i in md.disasm(img[h - base : h - base + 0x400], h):
            if i.mnemonic == "mov" and i.op_str.startswith("eax, 0x"):
                last = int(i.op_str[5:], 16)
            if i.mnemonic in ("ret", "jmp", "int3"):
                break
        if last is not None and in_code(last):
            eh.add(last)
            eh_cont.add(last)
for mt in _re.finditer(rb"\x68(....)(?:\x64\xA1\x00\x00\x00\x00|\x50\x64\x89\x25\x00\x00\x00\x00)", img, _re.S):
    h = _st.unpack("<I", mt.group(1))[0]  # push handler; mov eax, fs:[0] | push eax; mov fs:[0], esp
    if in_code(base + mt.start()) and in_code(h):
        eh.add(h)
# CRT __try (SEH): `push N; push scopetable; call __SEH_prolog` installs _except_handler3 and sets the
# caller's ebp. Scope entries {enclosing, filter, handler}: filters and __finally blocks are called,
# an __except block is jumped to like a catch continuation.
seh_prolog = []
for mt in _re.finditer(
    rb"\x68....\x64\xA1\x00\x00\x00\x00\x50\x8B\x44\x24\x10\x89\x6C\x24\x10\x8D\x6C\x24\x10", img, _re.S
):
    if in_code(base + mt.start()):
        seh_prolog.append(base + mt.start())
for mt in _re.finditer(rb"(?:\x6A.|\x68....)\x68(....)\xE8(....)", img, _re.S):
    call_va = base + mt.end() - 5
    if not in_code(call_va) or call_va + 5 + _st.unpack("<i", mt.group(2))[0] not in seh_prolog:
        continue
    st = _st.unpack("<I", mt.group(1))[0]
    for k in range(64):
        lvl, flt, hnd = _u32(st + 12 * k), _u32(st + 12 * k + 4), _u32(st + 12 * k + 8)
        if (
            lvl is None
            or not (lvl == 0xFFFFFFFF or lvl < k)
            or hnd is None
            or not in_code(hnd)
            or (flt and not in_code(flt))
        ):
            break
        eh.add(hnd)
        if flt:
            eh.add(flt)
            eh_cont.add(hnd)
eh = {a for a in eh if decodes_cleanly(a)}
src["eh_new"] = len(eh - entries)
entries |= eh

inv_ok = inv_bad = inv_noref = split = 0
for f in json.load(open(A / "functions.json"))["functions"]:
    a = f["address"]
    if a in entries or not in_text(a) or f.get("entry_kind", "start") != "start":
        continue
    if inside_ghidra(a):
        split += 1
    elif a not in datarefs:
        inv_noref += 1
    elif decodes_cleanly(a):
        entries.add(a)
        inv_ok += 1
    else:
        inv_bad += 1
src.update(invented_added=inv_ok, invented_rejected=inv_bad, invented_unreferenced=inv_noref, split_excluded=split)

src["emulated_targets_new"] = emulated_targets(entries)
src["call_targets_added"] = call_targets(entries)

# A start inside a patched `push R; push T; ret` site (and its junk gap) is never a function: the
# lifter reads that span as `call T; jmp R`. Seeds (fixpoint fall-throughs, invented starts) that land
# there would cut the enclosing function short and leave a 5-byte call in a 3-byte "function".
# The site start itself stays: a body cut just before it tail-jumps there (0x407599 -> 0x4075A0).
_inside_site = {a for s, _, r in OBF for a in range(s + 1, code_patch.gap_end(s, r))}
src["dropped_inside_obf_sites"] = len(entries & _inside_site)
entries -= _inside_site

out = sorted(entries)
json.dump(
    {
        "code_ranges": list(CODE_RANGES),
        "sources": src,
        "entries": out,
        "eh_continuations": sorted(eh_cont),
        "seh_prolog": seh_prolog,
    },
    open(A / "lift_catalog.json", "w"),
)
print(json.dumps(src), file=log)
print(f"build_catalog: {len(out)} entries {src}")
