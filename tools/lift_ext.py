"""Extensions to pcrecomp's lift32.Lifter for Rugby 08 (imported by tools/lift.py).

Every instruction gets the semantics of the CPU the runtime models (runtime/DESIGN.md: a
Pentium Pro class x86 -- FPU, TSC, CX8, CMOV/FCMOV; no MMX, SSE, SSE2 or 3DNow!):

  IMPLEMENTED here : valid on that CPU but missing from lift32 (fucompp, fcomi family, fcmov,
                     ficom, lahf, BCD adjusts, xadd, bound, arpl, into, fnstenv/fldenv, ...)
  #UD fault        : extensions the modeled CPU does not have (MMX/SSE/SSE2/SSE3/3DNow!/AVX/TSX).
                     A real CPU without them raises #UD; the game's CPUID dispatch never takes
                     them because our CPUID does not advertise them.
  #GP fault        : privileged / I/O / far-transfer / software-interrupt instructions, which
                     fault in user mode on real hardware.
Faults call recomp_fault(va, what) (noreturn, logs the address). Anything not classified is
left as lift32's UNIMPLEMENTED marker so tools/lift_report.sh still counts it.
"""
import lift32

FCMOV = {"fcmovb": "jb", "fcmove": "je", "fcmovbe": "jbe", "fcmovu": "jp",
         "fcmovnb": "jae", "fcmovne": "jne", "fcmovnbe": "ja", "fcmovnu": "jnp"}
GP = {"cli", "sti", "hlt", "in", "out", "insb", "insw", "insd", "outsb", "outsw", "outsd",
      "iret", "iretd", "int", "int1", "ljmp", "lcall", "syscall", "sysenter", "sysexit",
      "lgdt", "lidt", "lldt", "ltr", "clts", "invd", "wbinvd", "invlpg", "rdmsr", "wrmsr", "rsm",
      "rdpmc", "sysret", "into_gp"}
NOP_LIKE = {"pause", "fsetpm", "fnop", "prefetcht0", "prefetcht1", "prefetcht2", "prefetchnta",
            "prefetch", "prefetchw", "nop"}

EFL = "recomp_eflags(_flag_k, _flag_a, _flag_b, _cf, _df)"
SET_EFL = "_flag_k = FK_EFLAGS; _flag_a = _fl; _flag_b = 0; _cf = _fl & 1u;"


def _is_ud(insn):
    m, o = insn.mnemonic, insn.op_str
    if "xmm" in o or "ymm" in o or m.startswith("v") and len(m) > 4 and m not in ("verr", "verw"):
        return True
    if m.startswith("pf") or m in ("femms", "pi2fd", "pi2fw", "pf2id", "pf2iw", "pswapd",
                                   "pmulhrw", "pavgusb"):
        return True                                            # 3DNow!
    if m in ("fisttp", "movnti", "stmxcsr", "ldmxcsr", "sfence", "lfence", "mfence", "clflush",
             "xbegin", "xend", "xabort", "xtest", "notrack", "bnd", "endbr32", "ud2", "ud1", "ud0",
             "fxsave", "fxrstor", "maskmovq", "movntq", "pshufw", "pinsrw", "pextrw", "pmovmskb",
             "pminub", "pmaxub", "pminsw", "pmaxsw", "pavgb", "pavgw", "psadbw", "pmulhuw",
             "movq2dq", "movdq2q", "psubq", "paddq", "pmuludq", "cvtpi2ps", "cvtps2pi",
             "cvttps2pi", "cvtpi2pd", "cvtpd2pi", "cvttpd2pi"):
        return True
    if " mm" in " " + o or "mm0" in o or "mm1" in o or "mm2" in o or "mm3" in o or \
       "mm4" in o or "mm5" in o or "mm6" in o or "mm7" in o or m == "emms":
        return True                                            # MMX (not advertised)
    return False


class Lifter(lift32.Lifter):
    def _fault(self, insn, what, comment):
        return [f'recomp_fault(0x{insn.address:08X}u, "{what} {insn.mnemonic}"); '
                f'{{ RECOMP_REGS_OUT(); return; }} {comment}']

    def _eflags_op(self, body, comment):
        self._flag_state = ("eflags", "_flag_a, _flag_b")
        self._flag_seq += 1
        return [f"{{ uint32_t _fl = {EFL}; {body} {SET_EFL} }} {comment}"]

    def _lift_instruction(self, insn):
        m, ops = insn.mnemonic, insn.operands
        comment = f"/* 0x{insn.address:08X}: {insn.mnemonic} {insn.op_str} */"

        # --- ignored prefixes: F2 (bnd) and 3E (notrack) on a branch do nothing on this CPU ---
        if m.startswith(("bnd ", "notrack ")):
            insn.mnemonic = m.split(" ", 1)[1]
            try:
                return self._lift_instruction(insn)
            finally:
                insn.mnemonic = m

        # --- aliases onto lift32's own implementations ---------------------
        alias = {"fucompp": "fcompp", "fcompi": "fcomip", "fucompi": "fucomip",
                 "fcomi": "fcom", "fucomi": "fucom", "fstpnce": "fstp"}.get(m)
        if alias:
            insn.mnemonic = alias
            try:
                return super()._lift_instruction(insn)
            finally:
                insn.mnemonic = m

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
            return [f"{{ uint32_t _e = {addr}; memset((void*)ADDR(_e), 0, 28); MEM16(_e) = _fpu_cw; "
                    f"MEM16(_e + 4) = (uint16_t)(((8 - _fp_top) & 7) << 11); MEM16(_e + 8) = 0xFFFFu; }} {comment}"]

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
                r, lo, hi = f"(int16_t){self._fmt_read(ops[0])}", f"(int16_t)MEM16({addr})", f"(int16_t)MEM16(({addr}) + 2)"
            else:
                r, lo, hi = f"(int32_t){self._fmt_read(ops[0])}", f"(int32_t)MEM32({addr})", f"(int32_t)MEM32(({addr}) + 4)"
            return [f'if ({r} < {lo} || {r} > {hi}) recomp_fault(0x{insn.address:08X}u, "#BR bound"); {comment}']
        if m == "arpl":
            d, s = self._fmt_read(ops[0]), self._fmt_read(ops[1])
            w = self._fmt_write(ops[0], "(uint16_t)((_d & ~3u) | (_s & 3u))")
            return self._eflags_op(f"uint32_t _d = (uint16_t)({d}), _s = (uint16_t)({s}); "
                                   f"if ((_d & 3u) < (_s & 3u)) {{ {w}; _fl |= 0x40u; }} else _fl &= ~0x40u;",
                                   comment)
        if m == "popaw":
            return [f"{{ SET_LO16(edi, POP16_VAL(esp)); SET_LO16(esi, POP16_VAL(esp)); SET_LO16(ebp, POP16_VAL(esp)); "
                    f"esp += 2; SET_LO16(ebx, POP16_VAL(esp)); SET_LO16(edx, POP16_VAL(esp)); "
                    f"SET_LO16(ecx, POP16_VAL(esp)); SET_LO16(eax, POP16_VAL(esp)); }} {comment}"]
        if m == "xadd" and len(ops) == 2:
            a, b = self._fmt_read(ops[0]), self._fmt_read(ops[1])
            lines = [f"{{ uint32_t _xa = {a}, _xb = {b}; {comment}"]
            lines.append(self._flag_capture("_xa", "_xb", lift32.op_bits(ops[0])))
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
