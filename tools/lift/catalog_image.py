"""The image the catalog is built from (tools/lift/build_catalog.py): the exe with the obfuscated call sites
rewritten as the lifter decodes them (code_patch), the Ghidra function list, and the predicates on addresses
every seed source uses."""

import bisect
import csv
import pathlib
import sys

import pefile
from capstone import CS_ARCH_X86, CS_MODE_32, Cs

root = pathlib.Path(__file__).resolve().parent.parent.parent
A = root / "analysis"
pe = pefile.PE(str(root / "Rugby08_unpacked.exe"), fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
img = pe.get_memory_mapped_image()
sys.path.insert(0, str(root / "tools/lift"))
import code_patch  # noqa: E402
from ranges import CODE_HI, CODE_LO, TEXT, in_code  # noqa: E402

T_LO, T_HI = TEXT
_pcode, OBF = code_patch.patch(img[CODE_LO - base : CODE_HI - base], CODE_LO, CODE_HI, img[: CODE_LO - base])
img = img[: CODE_LO - base] + _pcode + img[CODE_HI - base :]  # decode what the lifter decodes
md = Cs(CS_ARCH_X86, CS_MODE_32)
log = open(root / "logs/catalog.log", "w")


def in_text(a):
    return in_code(a)  # .text or the protector region (tools/lift/ranges.py)


ghidra = []
for s, e in csv.reader(open(A / "ghidra_funcs.csv")):
    s, e = int(s, 16), int(e, 16)
    if in_text(s):
        ghidra.append((s, e))
ghidra.sort()
g_starts = [s for s, _ in ghidra]


def inside_ghidra(a):
    i = bisect.bisect_right(g_starts, a) - 1
    return i >= 0 and g_starts[i] < a < ghidra[i][1] and ghidra[i][1] - g_starts[i] < 0x40000


IMG_END = base + pe.OPTIONAL_HEADER.SizeOfImage


def bad_target(t):
    """A direct branch target real code cannot have: outside the image, in the PE header, or zero fill
    (`00 00`, never the start of an emitted instruction). Not "outside the code ranges": protector
    code calls SecuROM's runtime library in the dump's merged .data (0xC84600, 0xC85020 _chkstk)."""
    return not (TEXT[0] <= t < IMG_END) or img[t - base : t - base + 2] == b"\0\0"


def decodes_cleanly(a, limit=4096, walk=512):
    """True if linear decode from `a` reaches ret/jmp before any invalid byte or int3, and no direct
    branch reachable from `a` (both arms of every jcc, jmp targets, call targets; at most `walk`
    instructions) has a bad_target: protector constants decoded as code do (0x457418: `jmp 0x40017A`
    into the PE header, 0xC8930F9 past the image, 29 tails into zero fill reached through a jcc).
    int3 ends a walked path (padding after a call that does not return), it does not reject."""
    n = 0
    for i in md.disasm(img[a - base : a - base + limit], a):
        n += 1
        if i.mnemonic in ("int3", "hlt") or i.mnemonic.startswith("(bad"):
            return False
        if i.mnemonic in ("ret", "retn", "jmp"):
            break
    else:
        return False
    todo, seen = [a], set()
    while todo and len(seen) < walk:
        va = todo.pop()
        for i in md.disasm(img[va - base : va - base + 256], va):
            if i.address in seen or i.mnemonic == "int3":
                break
            seen.add(i.address)
            if i.mnemonic == "hlt" or i.mnemonic.startswith("(bad"):
                return False
            direct = i.op_str.startswith("0x") and (i.mnemonic == "call" or i.mnemonic.startswith("j"))
            if direct:
                t = int(i.op_str, 16)
                if bad_target(t):
                    return False
                if i.mnemonic != "call":
                    todo.append(t)
            if i.mnemonic in ("ret", "retn", "jmp"):
                break
    return True
