"""Where code lives in Rugby08_unpacked.exe (REVERSE.md "Protector region").

The dump merged the protector's payload section (.IN, 0xE65000..0x1457000, plus the .HEAVEN
stub) into .data, and the protector relocated game functions there: .text reaches them through
`jmp/call [ptr]` stubs, triggers and direct calls. Both ranges are code for every tool."""
TEXT = (0x401000, 0x8CC000)
PROT = (0xE65000, 0x1463000)
CODE_RANGES = (TEXT, PROT)
CODE_LO, CODE_HI = TEXT[0], PROT[1]     # one buffer covering both (the lifter decodes from it)


def in_code(a: int) -> bool:
    return TEXT[0] <= a < TEXT[1] or PROT[0] <= a < PROT[1]
