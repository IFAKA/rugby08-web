"""Resolve the protector's computed-jump stubs by running them on the static image (unicorn).

A switch slot can point at a stub such as 0x45E637 `push ebp; clc; mov ebp, [0x1381894];
adc ebp, [0x4795E1]; ...; xchg [esp], ebp; ret` that rebuilds the real case address from constants
in the image and jumps there. The constants are static, so executing the stub from its start with
an empty stack lands exactly where the game would: the first ret or indirect jmp that leaves the
stack as it was on entry is the stub's jump, and its destination is the target."""

from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_MODE_32, Uc, UcError
from unicorn.x86_const import UC_X86_REG_ESP

STACK = 0x7F000000


class StubEmu:
    def __init__(self, base, img):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        size = (len(img) + 0xFFF) & ~0xFFF
        self.uc.mem_map(base, size)
        self.uc.mem_write(base, bytes(img))
        self.uc.mem_map(STACK - 0x10000, 0x20000)
        self.base, self.img = base, bytes(img)
        self.dirty = []
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._on_write)

    def _on_write(self, u, access, addr, size, value, _):
        if self.base <= addr < self.base + len(self.img):
            self.dirty.append((addr, size))

    def target(self, va, max_insns=400):
        """Where the stub at va jumps with a balanced stack, or None (calls, faults, too long)."""
        uc, hit = self.uc, []
        for addr, size in self.dirty:  # stubs may write their own data; start clean
            o = addr - self.base
            uc.mem_write(addr, self.img[o : o + size])
        self.dirty = []
        uc.reg_write(UC_X86_REG_ESP, STACK)

        def on_code(u, addr, size, _):
            if addr != va and u.reg_read(UC_X86_REG_ESP) == STACK and hit and hit[-1] == "leave":
                hit.append(addr)
                u.emu_stop()
                return
            insn = u.mem_read(addr, min(size, 16))
            # ret (C3/C2) or jmp r/m (FF /4) may be the transfer; check the stack at the next insn
            hit.append("leave" if insn[0] in (0xC3, 0xC2) or (insn[0] == 0xFF and (insn[1] >> 3) & 7 == 4) else "step")

        h = uc.hook_add(UC_HOOK_CODE, on_code)
        try:
            uc.emu_start(va, 0, count=max_insns)
        except UcError:
            return None
        finally:
            uc.hook_del(h)
        return hit[-1] if hit and isinstance(hit[-1], int) else None

    def transfers(self, va, max_insns=2000):
        """Every computed control transfer while running from va: destinations of indirect jmp/call
        and of rets that do not pop a return address this run pushed with a call. Follows chains
        (stub -> overlapping block 0x13A2D97 -> ...) until a fault or max_insns."""
        uc, out, rets, prev = self.uc, set(), set(), [None]
        for addr, size in self.dirty:
            o = addr - self.base
            uc.mem_write(addr, self.img[o : o + size])
        self.dirty = []
        uc.reg_write(UC_X86_REG_ESP, STACK)

        def on_code(u, addr, size, _):
            p = prev[0]
            if p is not None:
                kind, after = p
                if kind == "ret" and addr not in rets or kind == "ind":
                    out.add(addr)
            b = u.mem_read(addr, min(size, 16))
            op = b[0]
            if op == 0xE8:
                rets.add(addr + size)
                prev[0] = None
            elif op == 0xFF and len(b) > 1 and (b[1] >> 3) & 7 == 2:
                rets.add(addr + size)
                prev[0] = ("ind", None)
            elif op == 0xFF and len(b) > 1 and (b[1] >> 3) & 7 == 4:
                prev[0] = ("ind", None)
            elif op in (0xC3, 0xC2):
                prev[0] = ("ret", None)
            else:
                prev[0] = None

        h = uc.hook_add(UC_HOOK_CODE, on_code)
        try:
            uc.emu_start(va, 0, count=max_insns)
        except UcError:
            pass
        finally:
            uc.hook_del(h)
        return out
