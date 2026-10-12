"""Where code lives in Rugby08_unpacked.exe (REVERSE.md "Protector region").

The dump merged the protector's payload section (.IN, 0xE65000..0x1457000, plus the .HEAVEN
stub) into .data, and the protector relocated game functions there: .text reaches them through
`jmp/call [ptr]` stubs, triggers and direct calls. SECUROM: the original .ROT section (0x401000..
0xE65000, filled by the unpacker) ends with SecuROM's own runtime library (0xC84600, _chkstk at
0xC85020, thunk tables at 0xAE8780), called from the protector region; game data before it ends in
zero fill (0x9C0000..0xAC0000). All three ranges are code for every tool."""

TEXT = (0x401000, 0x8CC000)
SECUROM = (0xAC0000, 0xE65000)
PROT = (0xE65000, 0x1463000)
CODE_RANGES = (TEXT, SECUROM, PROT)
CODE_LO, CODE_HI = TEXT[0], PROT[1]  # one buffer covering both (the lifter decodes from it)


def in_code(a: int) -> bool:
    return TEXT[0] <= a < TEXT[1] or SECUROM[0] <= a < PROT[1]


# CRT initializer tables walked by _cinit (0x875BA8): __xi_a..__xi_z (C, return int) and
# __xc_a..__xc_z (C++ static constructors). Entries are reached only through these tables.
INITTERM = ((0x94B4C8, 0x94B4E0), (0x947000, 0x94B4C4))
