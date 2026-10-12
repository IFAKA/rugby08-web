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
from lift_ext_tables import (
    DIV,
    EFL,
    FCMOV,
    GP,
    NOP_LIKE,
    SET_EFL,
    _is_ud,
)


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
