"""Instruction tables for tools/lift/lift_ext.py: fcmov conditions, the general-purpose instructions
lift32 lacks, the ones that act as nops, division with #DE, MMX extras, and the #UD classifier."""

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
