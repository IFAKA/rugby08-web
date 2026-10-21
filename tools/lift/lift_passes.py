"""The text passes lift.py runs over each lifted body (pcrecomp lift32 output): carry flags across
labels, ret-as-jump, SEH wrapping, jumps into zero fill, overlapping decodes."""

import re


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
    return RET_RE.sub(
        lambda m: (
            f"{{ uint32_t _ra = MEM32(esp); esp += {m.group(1)}; if (_ra != RECOMP_RETADDR) "
            f"{{ RECOMP_ITAIL(_ra); {{ RECOMP_REGS_OUT(); return; }} }} }} "
            f"{{ RECOMP_REGS_OUT(); return; }} {m.group(2)}"
        ),
        code,
    )


EH_INSTALL = "MEM32(FS_BASE + (0)) = esp;"


def eh_wrap(code, name, addr, seh_prolog, eh_cont):
    """A body that installs an EH frame (`mov fs:[0], esp`) runs under a landing pad (RECOMP_EH_WRAP,
    tools/lift/vendor_recomp_types.py): the pad records ebp at the install, and a catch's continuation
    (analysis/lift_catalog.json eh_continuations) resumes at its label here, or tail-jumps to it when
    another fragment of the function holds it."""
    seh = [c for c in (f"RECOMP_CALL(sub_{v:08X});" for v in seh_prolog) if c in code]
    if EH_INSTALL not in code and not seh:
        return code
    head = f"void {name}(void) {{"
    body = f"{name}_eh"
    code = code.replace(head, f"static void {body}(void) {{\n    RECOMP_EH_BODY_ENTER();", 1)
    code = code.replace(EH_INSTALL, EH_INSTALL + " _eh_pad->ebp = ebp;")
    for c in seh:  # CRT __try: __SEH_prolog installs the frame and returns with ebp set to it
        code = code.replace(c, c + " _eh_pad->ebp = ebp;")
    labels = set(re.findall(r"^L_([0-9A-F]{8}):", code, re.M))
    cases = "".join(f" case 0x{l}u: goto L_{l};" for l in sorted(labels) if int(l, 16) in eh_cont)
    resume = (
        f"    if (_eh_resume) {{ RECOMP_REGS_IN(); switch (_eh_resume) {{{cases} default: "
        f"RECOMP_FLAGS_OUT(); RECOMP_ITAIL(_eh_resume); {{ RECOMP_REGS_OUT(); return; }} }} }}"
    )
    enter = f"    RECOMP_ENTER(0x{addr:08X}u);"
    code = code.replace(enter, enter + "\n" + resume, 1)
    sep = "" if code.endswith("\n") else "\n"  # the wrapper on its own line: tools parse ^void sub_
    return code + sep + f"void {name}(void) {{ RECOMP_EH_WRAP({body}); }}\n"


# a constant-target ITAIL is always a direct branch: an out-of-body label or the body's fall-through
_TAIL_LABEL = re.compile(r"RECOMP_FLAGS_OUT\(\); RECOMP_ITAIL\(0x([0-9A-F]{8})u\);")


def zero_fill_tails(code, buf, cs):
    """A branch out of the body into zero fill (`00 00`) is not a tail call: compiled code never
    starts there (tools/lift/lift_fixpoint.sh does not seed it either). It is the fall-through after a
    call that does not return, followed by padding; reaching it is a fault, not a jump."""

    def sub(m):
        t = int(m.group(1), 16)
        if 0 <= t - cs < len(buf) - 1 and buf[t - cs : t - cs + 2] == b"\0\0":
            return f'recomp_fault(0x{t:08X}u, "jump into zero fill");'
        return m.group(0)

    return _TAIL_LABEL.sub(sub, code)


def overlap_fallthroughs(trimmed):
    """(b, end) for consecutive instructions a, b where a can fall through but b does not start at
    a's end: the protector's overlapping decodes (0x432D10 `jmp 0x432D02` into the middle of the
    7-byte `mov byte [ebp-0x8A1], 0` at 0x432CFD, whose last bytes read `inc dword [eax]`). Emitted
    in address order, the mov fell into the inc the CPU never runs on that path; a's fall-through
    goes to its own end instead."""
    return [
        (b.address, a.end_address)
        for a, b in zip(trimmed, trimmed[1:])
        if not (a.is_ret or a.is_uncond_jump) and a.end_address != b.address
    ]


def overlap_gotos(code, falls, starts):
    for b, end in falls:
        lab = f"\nL_{b:08X}:"
        if lab not in code:
            continue
        jump = (
            f"    goto L_{end:08X};"
            if end in starts
            else f"    RECOMP_FLAGS_OUT(); RECOMP_ITAIL(0x{end:08X}u); {{ RECOMP_REGS_OUT(); return; }}"
        )
        code = code.replace(lab, f"\n{jump} /* fall-through of an overlapping decode */{lab}", 1)
    return code


FP_USE = re.compile(r"\b(_st|_fp_top)\b|\bfp_(push|pop)\(")


def fp_locals(code):
    """A body that touches the x87 stack keeps it in locals (runtime/include/recomp/recomp_fp.h)."""
    if not FP_USE.search(code):
        return code
    return code.replace("RECOMP_REGS_LOCALS\n", "RECOMP_REGS_LOCALS\n    RECOMP_FP_LOCALS\n", 1)
