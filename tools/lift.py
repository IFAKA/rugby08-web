#!/usr/bin/env python3
"""Lift every entry of analysis/lift_catalog.json to C in gen/recomp/ with pcrecomp's lifter.

Reuses pcrecomp tools/lift/generate.py (linear decode between catalog entries, trim after ret,
RECOMP_LOCAL_REGS-safe bodies, local switch dispatch) -- only the entry list is ours, because
generate.py finds entries heuristically and the translator re-runs discovery.
Parallel over chunks (multiprocessing). Prints one line; details -> logs/lift.log.
"""
import json, os, pathlib, re, sys, time
from multiprocessing import Pool

root = pathlib.Path(__file__).resolve().parent.parent
PC = root / "third_party/pcrecomp/tools"
for d in ("pe", "disasm", "lift"):
    sys.path.insert(0, str(PC / d))
sys.path.insert(0, str(root / "tools"))
import generate, lift32, lift_ext, code_patch, ranges, jumptables     # noqa: E402
from pe_analyze import analyze_pe, build_iat_map          # noqa: E402
from capstone import Cs, CS_ARCH_X86, CS_MODE_32          # noqa: E402

EXE = str(root / "Rugby08_unpacked.exe")
OUT = root / "gen/recomp"
SPLIT = 400
_G = {}

def _init():
    info = analyze_pe(EXE)
    pe_data = open(EXE, "rb").read()
    # One buffer over both code ranges (tools/ranges.py): .text and the protector region inside
    # .data. The dump is flat (raw == virtual), so image bytes are file bytes from ImageBase.
    import pefile
    img = pefile.PE(EXE, fast_load=True).get_memory_mapped_image()
    raw = img[ranges.CODE_LO - info.image_base:ranges.CODE_HI - info.image_base]
    _G["code"], _G["obf_sites"] = code_patch.patch(raw, ranges.CODE_LO, ranges.CODE_HI, img[:ranges.CODE_LO - info.image_base])
    _G["code_start"] = ranges.CODE_LO
    _G["code_end"] = ranges.CODE_HI
    _G["iat"] = build_iat_map(info)
    catj = json.load(open(root / "analysis/lift_catalog.json"))
    cat = catj["entries"]
    _G["entries"] = cat
    _G["eh_cont"] = set(catj.get("eh_continuations", []))
    _G["seh_prolog"] = catj.get("seh_prolog", [])
    _G["hooks"] = {int(k, 16): v for k, v in json.load(open(root / "tools/hooks.json")).items()}
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    _G["md"] = md
    _G["end"] = body_ends(cat, ranges.CODE_HI)
    lifter = lift_ext.Lifter(iat_map=_G["iat"])
    lifter.lifted = set(cat)     # calls to anything else go through RECOMP_ICALL (logged miss)
    _G["lifter"] = lifter

def body_ends(entries, code_end):
    """Where each entry's lifted body stops (see runtime/DESIGN.md "Function bodies").
    Ghidra functions are lifted over their whole extent -- decoding straight over any other entry
    inside them -- so loops stay local instead of becoming chains of tail calls. An entry inside a
    Ghidra function runs to that function's end; anything else stops at the next top-level entry."""
    import bisect, csv
    g = []
    for s, e in csv.reader(open(root / "analysis/ghidra_funcs.csv")):
        s, e = int(s, 16), int(e, 16)
        if 0 < e - s <= 0x40000 and not truncated_extent(s, e):
            g.append((s, e))
    g.sort()
    gs = [s for s, _ in g]
    for i in range(len(g) - 1):             # contiguous MSVC bodies: never past the next function
        if g[i][1] > g[i + 1][0]:
            g[i] = (g[i][0], g[i + 1][0])
    gend = dict(g)
    def host(a):
        i = bisect.bisect_right(gs, a) - 1
        return g[i] if i >= 0 and g[i][0] <= a < g[i][1] else None
    top = sorted(a for a in entries if a in gend or host(a) is None)
    ends = {}
    for a in entries:
        h = host(a)
        if h:
            ends[a] = h[1]
        else:
            j = bisect.bisect_right(top, a)
            e = min(top[j] if j < len(top) else code_end, a + 0x10000)
            if a < ranges.TEXT[1] < e:          # never run from .text into .data
                e = ranges.TEXT[1]
            ends[a] = e
    return ends

def truncated_extent(s, e):
    """Ghidra stopped some protector stubs after their first instruction (0xAC5440 is
    `pushal` then [s+1, e) = nothing: the body was cut to one instruction and tail-jumped to an
    address nobody lifts). A tiny extent whose last instruction does not end the flow (ret, jmp,
    int3) is not a real extent; the entry then runs to the next top-level entry instead."""
    if e - s > 8 or not (_G["code_start"] <= s and e <= _G["code_end"]):
        return False
    last = None
    for i in _G["md"].disasm(_G["code"][s - _G["code_start"]:e - _G["code_start"]], s):
        last = i
    if last is None or last.address + last.size != e:
        return True
    return not (last.mnemonic.startswith("ret") or last.mnemonic == "jmp" or last.mnemonic == "int3")

def straddle_end(addr, end):
    """Catalog entries can start inside an instruction (seeds taken from garbage decodes): one at
    0x4132B1 cut sub_004132AD's 5-byte jmp to 4 bytes, which decoded to nothing (#UD empty entry).
    Extend the body over an instruction that straddles its end."""
    code, cs = _G["code"], _G["code_start"]
    for i in _G["md"].disasm(code[addr - cs:min(end + 16, _G["code_end"]) - cs], addr):
        if i.address >= end:
            break
        if i.address + i.size > end:
            return i.address + i.size
    return end

def carry_flags(code):
    """Flags live in locals; a ret exports them and a call imports them, but a tail jump did
    neither. The protector splits `cmp` from its `jcc` across stolen fragments (0x4125F6: cmp
    eax, [0x13834B0]; jmp 0x6CFB28: je ...), and MSVC's /RTC `cmp esi, esp; call _RTC_CheckEsp`
    tests the caller's flags inside the callee. So every body starts from the global flags and
    every tail jump and call exports them first."""
    code = code.replace("uint32_t _cf = 0;", "uint32_t _cf = g_flag_cf;", 1)
    code = code.replace("uint32_t _flag_k = FK_NONE;", "uint32_t _flag_k = g_flag_k;", 1)
    code = code.replace("uint32_t _flag_a = 0, _flag_b = 0;", "uint32_t _flag_a = g_flag_a, _flag_b = g_flag_b;", 1)
    for m in ("RECOMP_ITAIL(", "RECOMP_CALL(", "RECOMP_ICALL("):
        code = code.replace(m, "RECOMP_FLAGS_OUT(); " + m)
    return code

RET_RE = re.compile(r"esp \+= (\d+); \{ RECOMP_REGS_OUT\(\); return; \} (/\* 0x[0-9A-F]{8}: ret)")

def ret_jumps(code):
    """A lifted call pushes RECOMP_RETADDR, so a `ret` that pops anything else is a computed jump:
    the protector's `push T; ret`, `xchg [esp], reg; ret` and `pushfd; add [esp+4], K; popfd` + a jmp
    into the `ret` byte of that add (0x1042FA5 -> 0x1042FAA -> 0xB01CBE). Before, every ret returned
    to the host caller and the computed target never ran; now it is a tail jump to that target."""
    return RET_RE.sub(lambda m: (f"{{ uint32_t _ra = MEM32(esp); esp += {m.group(1)}; if (_ra != RECOMP_RETADDR) "
                                 f"{{ RECOMP_ITAIL(_ra); {{ RECOMP_REGS_OUT(); return; }} }} }} "
                                 f"{{ RECOMP_REGS_OUT(); return; }} {m.group(2)}"), code)

def reachable(insns, addr, end, arms):
    """The instructions of [addr, end) control flow reaches. After ret or an unconditional jmp the
    next bytes are reachable only through a label: the entry, a table arm, or a branch target of a
    kept instruction. Every other way in is a catalog entry of its own (tools/build_catalog.py:
    switch arms outside the body, EH funclets, catch and __except continuations, pointer tables).
    Not the sweep's leaders: pcrecomp marks every byte after a jmp a leader, so the protector's junk
    there was lifted and its fall-throughs became seeds (0x4600CD -> 0x4600D2 cut `jmp [0x1380008]`;
    with the EH funclets in the protector region, 13k junk seeds)."""
    labels = {addr} | arms
    while True:
        trimmed, dead = [], False
        for i in insns:
            if i.mnemonic == "int3":
                break
            if dead:
                if i.address not in labels:
                    continue
                dead = False
            trimmed.append(i)
            if i.is_ret or i.is_uncond_jump:
                dead = True
        new = {t for i in trimmed if i.is_jump and (t := i.get_branch_target()) is not None
               and addr <= t < end} - labels
        if not new:
            return trimmed
        labels |= new

EH_INSTALL = "MEM32(FS_BASE + (0)) = esp;"

def eh_wrap(code, name, addr):
    """A body that installs an EH frame (`mov fs:[0], esp`) runs under a landing pad (RECOMP_EH_WRAP,
    tools/vendor_recomp_types.py): the pad records ebp at the install, and a catch's continuation
    (analysis/lift_catalog.json eh_continuations) resumes at its label here, or tail-jumps to it when
    another fragment of the function holds it."""
    seh = [c for c in (f"RECOMP_CALL(sub_{v:08X});" for v in _G["seh_prolog"]) if c in code]
    if EH_INSTALL not in code and not seh:
        return code
    head = f"void {name}(void) {{"
    body = f"{name}_eh"
    code = code.replace(head, f"static void {body}(void) {{\n    RECOMP_EH_BODY_ENTER();", 1)
    code = code.replace(EH_INSTALL, EH_INSTALL + " _eh_pad->ebp = ebp;")
    for c in seh:       # CRT __try: __SEH_prolog installs the frame and returns with ebp set to it
        code = code.replace(c, c + " _eh_pad->ebp = ebp;")
    labels = set(re.findall(r"^L_([0-9A-F]{8}):", code, re.M))
    cases = "".join(f" case 0x{l}u: goto L_{l};" for l in sorted(labels) if int(l, 16) in _G["eh_cont"])
    resume = (f"    if (_eh_resume) {{ RECOMP_REGS_IN(); switch (_eh_resume) {{{cases} default: "
              f"RECOMP_FLAGS_OUT(); RECOMP_ITAIL(_eh_resume); {{ RECOMP_REGS_OUT(); return; }} }} }}")
    enter = f"    RECOMP_ENTER(0x{addr:08X}u);"
    code = code.replace(enter, enter + "\n" + resume, 1)
    sep = "" if code.endswith("\n") else "\n"   # the wrapper on its own line: tools parse ^void sub_
    return code + sep + f"void {name}(void) {{ RECOMP_EH_WRAP({body}); }}\n"

# a constant-target ITAIL is always a direct branch: an out-of-body label or the body's fall-through
_TAIL_LABEL = re.compile(r"RECOMP_FLAGS_OUT\(\); RECOMP_ITAIL\(0x([0-9A-F]{8})u\);")

def zero_fill_tails(code):
    """A branch out of the body into zero fill (`00 00`) is not a tail call: compiled code never
    starts there (tools/lift_fixpoint.sh does not seed it either). It is the fall-through after a
    call that does not return, followed by padding; reaching it is a fault, not a jump."""
    buf, cs = _G["code"], _G["code_start"]
    def sub(m):
        t = int(m.group(1), 16)
        if 0 <= t - cs < len(buf) - 1 and buf[t - cs:t - cs + 2] == b"\0\0":
            return f'recomp_fault(0x{t:08X}u, "jump into zero fill");'
        return m.group(0)
    return _TAIL_LABEL.sub(sub, code)

def lift_one(idx):
    """Lift catalog entry idx -> (C code, ok). Shared by main() and tools/difftest.py."""
    E, code, cs, ce = _G["entries"], _G["code"], _G["code_start"], _G["code_end"]
    addr = E[idx]
    end = straddle_end(addr, _G["end"][addr])
    name = f"sub_{addr:08X}"
    try:
        arms = set()
        insns, leaders = jumptables.sweep(_G["md"], code, cs, addr, end, arms)
        trimmed = reachable(insns, addr, end, arms)
        if not trimmed:
            return f"void {name}(void) {{ recomp_fault(0x{addr:08X}u, \"#UD empty entry\"); }}\n", False
        code = generate.lift_function_linear(_G["lifter"], name, trimmed, leaders, addr)
        code = zero_fill_tails(carry_flags(ret_jumps(code)))
        hook = _G["hooks"].get(addr)
        if hook:   # tools/hooks.json: announce entry (checks grep for "<name> entered")
            code = code.replace(f"    RECOMP_ENTER(0x{addr:08X}u);",
                                f"    RECOMP_ENTER(0x{addr:08X}u);\n    rt_hook_enter(0x{addr:08X}u, \"{hook}\");", 1)
        return eh_wrap(code, name, addr), True
    except Exception as e:  # keep going; the report counts these
        return f"/* LIFT_ERROR {name}: {e} */\nvoid {name}(void) {{ }}\n", False

def _lift_chunk(job):
    file_idx, lo, hi = job
    funcs, errs = [], 0
    for idx in range(lo, hi):
        c, ok = lift_one(idx)
        funcs.append((c, _G["entries"][idx], f"sub_{_G['entries'][idx]:08X}"))
        errs += 0 if ok else 1
    generate.write_chunk(str(OUT), file_idx, funcs)
    return file_idx, hi - lo, errs

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for f in OUT.glob("recomp_*.c"):
        f.unlink()   # generated output only (gen/ is ours, gitignored)
    _init()
    E = _G["entries"]
    jobs = [(k, lo, min(lo + SPLIT, len(E))) for k, lo in enumerate(range(0, len(E), SPLIT))]
    t = time.time()
    with Pool(os.cpu_count(), initializer=_init) as p, open(root / "logs/lift.log", "w") as log:
        errs = 0
        for fi, n, e in p.imap_unordered(_lift_chunk, jobs):
            errs += e
            print(f"chunk {fi}: {n} funcs, {e} errors", file=log, flush=True)
    names = [f"sub_{a:08X}" for a in E]
    (OUT / "recomp_funcs.h").write_text("#pragma once\n#include <stdint.h>\n\n" +
        "".join(f"void {n}(void);\n" for n in names))
    (OUT / "recomp_dispatch.c").write_text('#include "recomp_types.h"\n#include "recomp_funcs.h"\n\n'
        "const recomp_dispatch_entry_t recomp_dispatch_table[] = {\n" +
        "".join(f"    {{ 0x{a:08X}u, sub_{a:08X} }},\n" for a in E) +
        f"}};\n\nconst uint32_t recomp_dispatch_count = {len(E)};\n"
        # catch continuations, sorted: only these may divert to a landing pad (runtime/win32/k32_eh.c)
        "\nconst uint32_t recomp_eh_cont[] = {" +
        ", ".join([f"0x{a:08X}u" for a in sorted(_G["eh_cont"])] + ["0"]) +
        f"}};\nconst uint32_t recomp_eh_cont_count = {len(_G['eh_cont'])};\n")
    size = sum(f.stat().st_size for f in OUT.glob("*.c"))
    print(f"lift: {len(E)} functions in {len(jobs)} files, {errs} errors, {size/1048576:.0f} MB C, {time.time()-t:.0f}s")

if __name__ == "__main__":
    main()
