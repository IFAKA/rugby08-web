"""Extensions to pcrecomp's lift32.Lifter for Rugby 08 (imported by tools/lift/lift.py).

Every instruction gets the semantics of the CPU the runtime models (runtime/DESIGN.md: a
Pentium II class x86 -- FPU, TSC, CX8, CMOV/FCMOV, MMX; no SSE, SSE2 or 3DNow!):

  IMPLEMENTED here : valid on that CPU but missing from lift32 (fucompp, fcomi family, fcmov,
                     ficom, lahf, BCD adjusts, xadd, bound, arpl, into, fnstenv/fldenv, ...)
  MMX              : lift32's MMX plus MMX_EXTRA (the VP6 decoder assumes MMX on every x86).
  #UD fault        : extensions the modeled CPU does not have (SSE/SSE2/SSE3/3DNow!/AVX/TSX,
                     including SSE's MMX-register additions such as pshufw).
                     A real CPU without them raises #UD; the game's CPUID dispatch never takes
                     them because our CPUID does not advertise them.
  #GP fault        : privileged / I/O / far-transfer / software-interrupt instructions, which
                     fault in user mode on real hardware.
Faults call recomp_fault(va, what) (noreturn, logs the address). Anything not classified is
left as lift32's UNIMPLEMENTED marker so tools/lift/lift_report.sh still counts it.
"""

import lift32

FCMOV = {
    "fcmovb": "jb",
    "fcmove": "je",
    "fcmovbe": "jbe",
    "fcmovu": "jp",
    "fcmovnb": "jae",
    "fcmovne": "jne",
    "fcmovnbe": "ja",
    "fcmovnu": "jnp",
}
GP = {
    "cli",
    "sti",
    "hlt",
    "in",
    "out",
    "insb",
    "insw",
    "insd",
    "outsb",
    "outsw",
    "outsd",
    "iret",
    "iretd",
    "int",
    "int1",
    "ljmp",
    "lcall",
    "syscall",
    "sysenter",
    "sysexit",
    "lgdt",
    "lidt",
    "lldt",
    "ltr",
    "clts",
    "invd",
    "wbinvd",
    "invlpg",
    "rdmsr",
    "wrmsr",
    "rsm",
    "rdpmc",
    "sysret",
    "into_gp",
    "sgdt",
    "sidt",
    "lmsw",
    "smsw",
    "str",
    "sldt_gp",
    "lar",
    "lsl",
    "verr",
    "verw",
    "invlpga",
    "swapgs",
}
NOP_LIKE = {
    "pause",
    "fsetpm",
    "fnop",
    "prefetcht0",
    "prefetcht1",
    "prefetcht2",
    "prefetchnta",
    "prefetch",
    "prefetchw",
    "nop",
}

EFL = "recomp_eflags(_flag_k, _flag_a, _flag_b, _cf, _df)"

# Before any compare the x87 condition codes read 0; lift32 starts _fpu_cmp at 0 = "equal"
# (C3 set). 3 = "no compare yet", which fnstsw encodes as 0x0000 (found by tools/lift/difftest.py
# on the CRT's _statusfp).
lift32.FUNCTION_LOCALS = tuple("int _fpu_cmp = 3;" if d == "int _fpu_cmp = 0;" else d for d in lift32.FUNCTION_LOCALS)

DIV = {  # (size, signed) -> C body; _s is the divisor read once; #DE -> soft fault, result 0
    (1, False): (
        "uint32_t _dv = (uint8_t)(_s), _n = LO16(eax);",
        "_dv == 0 || _n / _dv > 0xFFu",
        "SET_LO16(eax, ((_n % _dv) << 8) | (_n / _dv));",
        "SET_LO16(eax, 0);",
    ),
    (1, True): (
        "int32_t _dv = (int8_t)(_s), _n = (int16_t)LO16(eax);",
        "_dv == 0 || _n / _dv > 127 || _n / _dv < -128",
        "SET_LO16(eax, ((uint32_t)(uint8_t)(_n % _dv) << 8) | (uint8_t)(_n / _dv));",
        "SET_LO16(eax, 0);",
    ),
    (2, False): (
        "uint32_t _dv = (uint16_t)(_s), _n = (LO16(edx) << 16) | LO16(eax);",
        "_dv == 0 || _n / _dv > 0xFFFFu",
        "SET_LO16(eax, _n / _dv); SET_LO16(edx, _n % _dv);",
        "SET_LO16(eax, 0); SET_LO16(edx, 0);",
    ),
    (2, True): (
        "int32_t _dv = (int16_t)(_s), _n = (int32_t)((LO16(edx) << 16) | LO16(eax));",
        "_dv == 0 || (_dv == -1 && _n == INT32_MIN) || _n / _dv > 32767 || _n / _dv < -32768",
        "SET_LO16(eax, (uint16_t)(_n / _dv)); SET_LO16(edx, (uint16_t)(_n % _dv));",
        "SET_LO16(eax, 0); SET_LO16(edx, 0);",
    ),
    (4, False): (
        "uint64_t _dv = (uint32_t)(_s), _n = ((uint64_t)edx << 32) | eax;",
        "_dv == 0 || _n / _dv > 0xFFFFFFFFull",
        "eax = (uint32_t)(_n / _dv); edx = (uint32_t)(_n % _dv);",
        "eax = 0; edx = 0;",
    ),
    (4, True): (
        "int64_t _dv = (int32_t)(_s), _n = (int64_t)(((uint64_t)edx << 32) | eax);",
        "_dv == 0 || (_dv == -1 && _n == INT64_MIN) || _n / _dv > 2147483647LL || _n / _dv < -2147483648LL",
        "eax = (uint32_t)(int32_t)(_n / _dv); edx = (uint32_t)(int32_t)(_n % _dv);",
        "eax = 0; edx = 0;",
    ),
}
SET_EFL = "_flag_k = FK_EFLAGS; _flag_a = _fl; _flag_b = 0; _cf = _fl & 1u;"


# MMX (advertised, see tools/lift/vendor_recomp_types.py): lift32 implements most of the original set;
# the rest are runtime helpers (recomp_types.h "MMX instructions lift32 leaves out").
MMX_EXTRA = {
    m: f"mmx_{m}({{a}}, {{b}})"
    for m in (
        "paddb",
        "psubb",
        "paddsb",
        "psubsb",
        "paddusb",
        "psubusb",
        "paddusw",
        "psubusw",
        "pcmpeqb",
        "pcmpgtb",
        "pcmpeqd",
        "pcmpgtd",
        "packsswb",
    )
}
lift32.MMX_BINOPS.update(MMX_EXTRA)
MMX = set(lift32.MMX_BINOPS) | set(lift32.MMX_SHIFTS) | {"movq", "movd", "emms"}


def _is_ud(insn):
    m, o = insn.mnemonic, insn.op_str
    if "xmm" in o or "ymm" in o or m.startswith("v") and len(m) > 4 and m not in ("verr", "verw"):
        return True
    if m.startswith("pf") or m in ("femms", "pi2fd", "pi2fw", "pf2id", "pf2iw", "pswapd", "pmulhrw", "pavgusb"):
        return True  # 3DNow!
    if m in (
        "fisttp",
        "movnti",
        "stmxcsr",
        "ldmxcsr",
        "sfence",
        "lfence",
        "mfence",
        "clflush",
        "xbegin",
        "xend",
        "xabort",
        "xtest",
        "notrack",
        "bnd",
        "endbr32",
        "ud2",
        "ud1",
        "ud0",
        "fxsave",
        "fxrstor",
        "maskmovq",
        "movntq",
        "pshufw",
        "pinsrw",
        "pextrw",
        "pmovmskb",
        "pminub",
        "pmaxub",
        "pminsw",
        "pmaxsw",
        "pavgb",
        "pavgw",
        "psadbw",
        "pmulhuw",
        "movq2dq",
        "movdq2q",
        "psubq",
        "paddq",
        "pmuludq",
        "cvtpi2ps",
        "cvtps2pi",
        "cvttps2pi",
        "cvtpi2pd",
        "cvtpd2pi",
        "cvttpd2pi",
    ):
        return True
    if any(f"mm{k}" in o for k in range(8)) and m not in MMX:
        return True  # MMX-register forms outside MMX
    return False


class Lifter(lift32.Lifter):
    def _fault(self, insn, what, comment):
        return [
            f'recomp_fault(0x{insn.address:08X}u, "{what} {insn.mnemonic}"); {{ RECOMP_REGS_OUT(); return; }} {comment}'
        ]

    def _eflags_op(self, body, comment):
        self._flag_state = ("eflags", "_flag_a, _flag_b")
        self._flag_seq += 1
        return [f"{{ uint32_t _fl = {EFL}; {body} {SET_EFL} }} {comment}"]

    def _lift_instruction(self, insn):
        m, ops = insn.mnemonic, insn.operands
        comment = f"/* 0x{insn.address:08X}: {insn.mnemonic} {insn.op_str} */"

        # --- ignored prefixes: F2 (bnd) and 3E (notrack) on a branch do nothing on this CPU; a rep
        # prefix on a non-string instruction (AMD's `rep ret`) is ignored too ---
        STRING_OPS = ("movs", "stos", "lods", "cmps", "scas")
        if m.startswith(("rep ", "repz ", "repe ", "repne ", "repnz ")) and not m.split(" ", 1)[1].startswith(
            STRING_OPS
        ):
            insn.mnemonic = m.split(" ", 1)[1]
            try:
                return self._lift_instruction(insn)
            finally:
                insn.mnemonic = m
        if m.startswith(("bnd ", "notrack ")):
            insn.mnemonic = m.split(" ", 1)[1]
            try:
                return self._lift_instruction(insn)
            finally:
                insn.mnemonic = m

        # `call $+5; pop reg` reads its own address (get-PC; the protector's call dispatcher at
        # 0x1055006 finds its table this way). A real call there would push the dummy return
        # address, so it is lifted as what it is: push the next address and fall through.
        if (
            m == "call"
            and ops
            and ops[0].type == lift32.X86_OP_IMM
            and (ops[0].imm & 0xFFFFFFFF) == insn.address + insn.size
        ):
            return [f"PUSH32(esp, 0x{insn.address + insn.size:08X}u); {comment}"]

        # --- aliases onto lift32's own implementations ---------------------
        alias = {
            "fucompp": "fcompp",
            "fcompi": "fcomip",
            "fucompi": "fucomip",
            "fcomi": "fcom",
            "fucomi": "fucom",
            "fstpnce": "fstp",
        }.get(m)
        if alias:
            insn.mnemonic = alias
            try:
                return super()._lift_instruction(insn)
            finally:
                insn.mnemonic = m

        # lift32 skips fdivp/fdivrp by zero (leaves the destination); the x87 with ZE masked (the
        # CRT's default) gives +-Inf, and 0/0 gives NaN -- plain IEEE division. (difftest: sub_00783F60)
        if m == "fdivp":
            d = self._fpu_popdst(ops)
            return [f"{{ double _v = fp_pop(); {d} = {d} / _v; }} {comment}"]
        if m == "fdivrp":
            d = self._fpu_popdst(ops)
            return [f"{{ double _v = fp_pop(); {d} = _v / {d}; }} {comment}"]

        if m in ("div", "idiv") and len(ops) == 1:
            decl, bad, ok, zero = DIV[(ops[0].size, m == "idiv")]
            s = self._fmt_read(ops[0])
            return [
                f"{{ uint32_t _s = {s}; {decl} {comment}",
                f'  if ({bad}) {{ recomp_soft_fault(0x{insn.address:08X}u, "#DE {m}"); {zero} }}',
                f"  else {{ {ok} }} }}",
            ]

        if m in ("fist", "fistp") and ops and ops[0].type == lift32.X86_OP_MEM:
            addr = self._fmt_mem_addr(ops[0].mem)
            src = "fp_pop()" if m == "fistp" else "_st[0]"
            bits = {2: 16, 4: 32, 8: 64}[ops[0].size]
            return [f"MEM{bits}({addr}) = recomp_fist{bits}(fp_to_int({src})); {comment}"]

        # Privileged / I/O instructions fault in user mode even where lift32 has a stub (in/out).
        if m in GP:
            return self._fault(insn, "#GP", comment)
        # The modeled CPU has no MMX/SSE/3DNow!: #UD even where lift32 knows the instruction.
        if _is_ud(insn):
            return self._fault(insn, "#UD", comment)

        if m in ("fnstsw", "fstsw"):
            # lift32 builds C3..C0 from the last compare; add TOP (bits 11-13) = (8 - depth) & 7.
            lines = super()._lift_instruction(insn)
            return [
                l.replace("| ((_fpu_cmp", "| ((((8 - _fp_top) & 7) << 11)) | ((_fpu_cmp").replace(
                    "(uint16_t)((_fpu_cmp", "(uint16_t)(((8 - _fp_top) & 7) << 11) | (uint16_t)((_fpu_cmp"
                )
                for l in lines
            ]

        if m in NOP_LIKE:
            return [f"/* {m} */ {comment}"]

        if m in FCMOV:
            cond = self._make_condition(FCMOV[m])
            return [f"if ({cond}) _st[0] = {self._fmt_fpu_src(ops)}; {comment}"]

        if m in ("ficom", "ficomp"):
            addr = self._fmt_mem_addr(ops[0].mem)
            cast = "(int16_t)MEM16" if ops[0].size == 2 else "(int32_t)MEM32"
            lines = [f"_fpu_cmp = FPU_CMP(_st[0], (double){cast}({addr})); {comment}"]
            if m == "ficomp":
                lines.append("fp_pop();")
            lines.append("_flag_a = (uint32_t)_fpu_cmp; _flag_b = 0;")
            self._flag_state = ("fcom", "_fpu_cmp")
            self._flag_seq += 1
            return lines

        if m in ("fnstenv", "fstenv", "fldenv"):
            addr = self._fmt_mem_addr(ops[0].mem)
            if m == "fldenv":
                return [f"_fpu_cw = MEM16({addr}); {comment}"]
            return [
                f"{{ uint32_t _e = {addr}; memset((void*)ADDR(_e), 0, 28); MEM16(_e) = _fpu_cw; "
                f"MEM16(_e + 4) = (uint16_t)(((8 - _fp_top) & 7) << 11); MEM16(_e + 8) = 0xFFFFu; }} {comment}"
            ]

        if m == "fbld":
            addr = self._fmt_mem_addr(ops[0].mem)
            return [f"fp_push(recomp_fbld({addr})); {comment}"]
        if m == "fbstp":
            addr = self._fmt_mem_addr(ops[0].mem)
            return [f"recomp_fbstp({addr}, fp_round_cw(_st[0], _fpu_cw)); fp_pop(); {comment}"]

        if m == "lahf":
            return [f"SET_HI8(eax, ({EFL} & 0xD5u) | 2u); {comment}"]
        if m == "salc":
            return [f"SET_LO8(eax, ({EFL} & 1u) ? 0xFFu : 0u); {comment}"]
        if m in ("daa", "das", "aaa", "aas"):
            return self._eflags_op(f"recomp_bcd_{m}(&eax, &_fl);", comment)
        if m in ("aam", "aad"):
            imm = ops[0].imm & 0xFF if ops else 10
            if m == "aam" and imm == 0:
                return self._fault(insn, "#DE", comment)
            return self._eflags_op(f"recomp_bcd_{m}(&eax, &_fl, {imm}u);", comment)
        if m == "into":
            return [f'if ({EFL} & 0x800u) recomp_fault(0x{insn.address:08X}u, "#OF into"); {comment}']
        if m == "sldt":
            return [f"{self._fmt_write(ops[0], '0u')}; {comment}"]
        if m == "bound":
            addr = self._fmt_mem_addr(ops[1].mem)
            if ops[0].size == 2:
                r, lo, hi = (
                    f"(int16_t){self._fmt_read(ops[0])}",
                    f"(int16_t)MEM16({addr})",
                    f"(int16_t)MEM16(({addr}) + 2)",
                )
            else:
                r, lo, hi = (
                    f"(int32_t){self._fmt_read(ops[0])}",
                    f"(int32_t)MEM32({addr})",
                    f"(int32_t)MEM32(({addr}) + 4)",
                )
            return [f'if ({r} < {lo} || {r} > {hi}) recomp_fault(0x{insn.address:08X}u, "#BR bound"); {comment}']
        if m == "arpl":
            d, s = self._fmt_read(ops[0]), self._fmt_read(ops[1])
            w = self._fmt_write(ops[0], "(uint16_t)((_d & ~3u) | (_s & 3u))")
            return self._eflags_op(
                f"uint32_t _d = (uint16_t)({d}), _s = (uint16_t)({s}); "
                f"if ((_d & 3u) < (_s & 3u)) {{ {w}; _fl |= 0x40u; }} else _fl &= ~0x40u;",
                comment,
            )
        if m == "popf":  # 16-bit popf: low word of EFLAGS
            return self._eflags_op(
                "_fl = (_fl & 0xFFFF0000u) | POP16_VAL(esp); _df = (_fl & 0x400u) ? -1 : 1;", comment
            )
        if m == "popaw":
            return [
                f"{{ SET_LO16(edi, POP16_VAL(esp)); SET_LO16(esi, POP16_VAL(esp)); SET_LO16(ebp, POP16_VAL(esp)); "
                f"esp += 2; SET_LO16(ebx, POP16_VAL(esp)); SET_LO16(edx, POP16_VAL(esp)); "
                f"SET_LO16(ecx, POP16_VAL(esp)); SET_LO16(eax, POP16_VAL(esp)); }} {comment}"
            ]
        if m == "xadd" and len(ops) == 2:
            a, b = self._fmt_read(ops[0]), self._fmt_read(ops[1])
            lines = [f"{{ uint32_t _xa = {a}, _xb = {b}; {comment}"]
            lines.append(self._flag_capture("_xa", "_xb", lift32.op_bits(ops[0])))
            lines.append("_cf = (uint32_t)(_flag_a + _flag_b < _flag_a);")  # as lift32's add
            lines.append(f"{self._fmt_write(ops[0], '_xa + _xb')}; {self._fmt_write(ops[1], '_xa')}; }}")
            self._flag_state = ("add", "_flag_a, _flag_b")
            return lines

        lines = super()._lift_instruction(insn)
        if any("UNIMPLEMENTED" in l or "not yet implemented" in l for l in lines):
            if m in GP:
                return self._fault(insn, "#GP", comment)
            if _is_ud(insn):
                return self._fault(insn, "#UD", comment)
        return lines
