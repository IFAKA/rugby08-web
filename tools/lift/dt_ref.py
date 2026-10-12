"""tools/lift/difftest.py, part 2: the Unicorn reference runs and the comparison against the harness."""

import os
import re
import struct
import subprocess

from dt_build import DATA_LO, IMG_HI, IMG_LO, OUT_VA, RET_VA, SCR_LO, SCR_SZ, STK_LO, STK_SZ, B
from unicorn import UC_ARCH_X86, UC_HOOK_MEM_UNMAPPED, UC_MODE_32, Uc, UcError
from unicorn import x86_const as X


def run_reference(a, E, chosen, bodies, img, scr, rng):
    """Each chosen body in Unicorn from seeded random state: (harness case lines, reference results, excluded)."""
    cases, ref = [], {}
    excluded = {}
    for k, idx in enumerate(chosen):
        regs = [SCR_LO + (rng.randrange(0, SCR_SZ - 0x1000) & ~3) for _ in range(8)]
        regs[4] = STK_LO + 0xF000  # esp
        regs[5] = STK_LO + 0xFF00  # ebp: a frame above the args
        args = []
        for _ in range(8):
            c = rng.random()
            args.append(
                SCR_LO + (rng.randrange(0, SCR_SZ - 0x1000) & ~3)
                if c < 0.5
                else rng.randrange(0, 256)
                if c < 0.8
                else rng.getrandbits(32)
            )
        fail = None
        for pre in (0, 2):
            mu = Uc(UC_ARCH_X86, UC_MODE_32)
            mu.mem_map(IMG_LO, IMG_HI - IMG_LO)
            mu.mem_write(IMG_LO, img)
            mu.mem_map(SCR_LO, SCR_SZ)
            mu.mem_write(SCR_LO, scr)
            mu.mem_map(STK_LO, STK_SZ)
            mu.mem_map(OUT_VA, 0x1000)
            mu.mem_map(RET_VA, 0x1000)
            mu.mem_write(RET_VA, bytes([0xDD, 0x15]) + OUT_VA.to_bytes(4, "little"))
            pages = []

            def unmapped(uc, access, addr, size, value, ud):
                for pg in {addr & ~0xFFF, (addr + max(size, 1) - 1) & ~0xFFF}:
                    # The null page and kernel space (>= 2 GiB: the exe is not LARGEADDRESSAWARE)
                    # fault on Windows, as in the runtime's 2 GiB arena: such a case is not comparable.
                    if pg >= 0x80000000:
                        return False
                    if pg not in pages and len(pages) < 64 and pg not in (0,):
                        try:
                            uc.mem_map(pg, 0x1000)
                            pages.append(pg)
                        except UcError:
                            return False
                return True

            mu.hook_add(UC_HOOK_MEM_UNMAPPED, unmapped)
            esp = regs[4]
            for v in reversed(args):
                esp -= 4
                mu.mem_write(esp, v.to_bytes(4, "little"))
            esp -= 4
            mu.mem_write(esp, RET_VA.to_bytes(4, "little"))
            for rid, v in zip(
                (
                    X.UC_X86_REG_EAX,
                    X.UC_X86_REG_ECX,
                    X.UC_X86_REG_EDX,
                    X.UC_X86_REG_EBX,
                    X.UC_X86_REG_ESP,
                    X.UC_X86_REG_EBP,
                    X.UC_X86_REG_ESI,
                    X.UC_X86_REG_EDI,
                ),
                regs[:4] + [esp] + regs[5:],
            ):
                mu.reg_write(rid, v)
            mu.reg_write(X.UC_X86_REG_EFLAGS, 0x202)
            mu.reg_write(X.UC_X86_REG_FPCW, 0x027F)
            # Same values on the x87 stack in both machines: st1 = 1.5, st0 = -2.25 (functions like
            # the CRT's _CIsin take their argument there), used only when an empty-stack run underflows.
            mu.mem_write(OUT_VA + 0x10, struct.pack("<dd", 1.5, -2.25))
            mu.mem_write(
                RET_VA + 0x100,
                bytes([0xDD, 0x05])
                + (OUT_VA + 0x10).to_bytes(4, "little")
                + bytes([0xDD, 0x05])
                + (OUT_VA + 0x18).to_bytes(4, "little"),
            )
            try:
                if pre:
                    mu.emu_start(RET_VA + 0x100, RET_VA + 0x10C)
                mu.emu_start(E[idx], RET_VA, count=3_000_000)
            except UcError as e:
                fail = str(e)
                break
            if mu.reg_read(X.UC_X86_REG_EIP) != RET_VA:
                fail = "runaway"
                break
            # An empty-stack run that ends at depth 2..7 underflowed: the function takes its input
            # on the x87 stack, so rerun it with values there (pre=2).
            if pre == 0 and ((8 - ((mu.reg_read(X.UC_X86_REG_FPSW) >> 11) & 7)) & 7) not in (0, 1):
                fail = "x87 underflow"
                continue
            fail = None
            break
        # x87 exception flags (status word bits 0-5) are not modeled: only a problem when this body
        # reads the status word AND the reference actually raised one (e.g. ZE after x/0).
        if not fail and (mu.reg_read(X.UC_X86_REG_FPSW) & 0x3F) and re.search(r"fn?stsw", bodies[k]):
            fail = "x87 exception flags read"
        if fail:
            excluded[fail] = excluded.get(fail, 0) + 1
            continue
        depth = (8 - ((mu.reg_read(X.UC_X86_REG_FPSW) >> 11) & 7)) & 7
        if depth:
            try:
                mu.emu_start(RET_VA, RET_VA + 6)
            except UcError:
                pass
        rv = [
            mu.reg_read(r)
            for r in (
                X.UC_X86_REG_EAX,
                X.UC_X86_REG_ECX,
                X.UC_X86_REG_EDX,
                X.UC_X86_REG_EBX,
                X.UC_X86_REG_ESP,
                X.UC_X86_REG_EBP,
                X.UC_X86_REG_ESI,
                X.UC_X86_REG_EDI,
            )
        ]
        hx = 1469598103934665603
        for i, pg in enumerate(pages):
            hx ^= (fnv_c(mu.mem_read(pg, 0x1000)) + i) & 0xFFFFFFFFFFFFFFFF
        hashes = {
            "data": fnv_c(mu.mem_read(DATA_LO, IMG_HI - DATA_LO)),
            "scr": fnv_c(mu.mem_read(SCR_LO, SCR_SZ)),
            "stk": fnv_c(mu.mem_read(STK_LO, STK_SZ)),
            "out": fnv_c(mu.mem_read(OUT_VA, 8)),
            "pages": hx,
        }
        live = rv[4] - STK_LO if STK_LO <= rv[4] <= STK_LO + STK_SZ else 0
        hashes["live"] = fnv_c(
            canon(bytes(mu.mem_read(STK_LO, STK_SZ)))[live:]
        )  # as the harness: after the whole stack
        ref[k] = (rv, depth, hashes)
        if a.dump == f"sub_{E[idx]:08X}":
            (B / "dump_uc.bin").write_bytes(bytes(mu.mem_read(SCR_LO, SCR_SZ)) + bytes(mu.mem_read(STK_LO, STK_SZ)))
            os.environ["DT_DUMP_IDX"] = str(k)
        cases.append(
            f"{k} "
            + " ".join(f"{v:08X}" for v in regs)
            + " "
            + " ".join(f"{v:08X}" for v in args)
            + f" {pre} {len(pages)} "
            + " ".join(f"{p:08X}" for p in pages)
        )
    return cases, ref, excluded


def compare(stdout, ref, E, chosen, excluded, mname):
    """One harness run's lines against the reference: (compared, mismatches, failure lines)."""
    compared = mism = 0
    fails = []
    for line in stdout.splitlines():
        f = line.split()
        k, st = int(f[0]), int(f[1])
        rv, depth, h = ref[k]
        name = f"sub_{E[chosen[k]]:08X}"
        if st == 5:  # computed jump out of the body (difftest_harness.c recomp_miss)
            excluded["computed jump"] = excluded.get("computed jump", 0) + 1
            continue
        if st != 0:
            fails.append(f"{mname} {name}: status {st} (1=signal 2=timeout 3=fault) but reference returned")
            mism += 1
            compared += 1
            continue
        got = [int(x, 16) for x in f[2:10]]
        diffs = [n for n, g, w in zip(["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"], got, rv) if g != w]
        if int(f[10]) != depth:
            diffs.append(f"fpdepth {f[10]}!={depth}")
        if int(f[11], 16) != h["data"]:
            diffs.append(".data")
        if int(f[12], 16) != h["scr"]:
            diffs.append("scratch")
        if mname == "thunk":  # the thunk's own call/push leave other dead slots: live stack only
            if int(f[16], 16) != h["live"]:
                diffs.append("live stack")
        elif int(f[13], 16) != h["stk"]:
            diffs.append("stack")
        if int(f[14], 16) != h["pages"]:
            diffs.append("pages")
        if depth and int(f[15], 16) != h["out"]:
            diffs.append("st0")
        compared += 1
        if diffs:
            mism += 1
            fails.append(
                f"{mname} {name}: "
                + " ".join(diffs)
                + f"  got {' '.join(f[2:10])} want {' '.join(f'{v:08X}' for v in rv)}"
            )
    return compared, mism, fails


_FNV_LIB = None


def canon(b):
    """NaN canonicalisation, identical to the harness's canon(): doubles, then floats."""
    import numpy as np

    a = bytearray(b)
    n8 = len(a) // 8 * 8
    q = np.frombuffer(a, dtype=np.uint64, count=n8 // 8)
    q[((q >> np.uint64(52)) & np.uint64(0x7FF) == np.uint64(0x7FF)) & ((q & np.uint64(0xFFFFFFFFFFFFF)) != 0)] = (
        np.uint64(0x7FF8000000000000)
    )
    d = np.frombuffer(a, dtype=np.uint32, count=len(a) // 4)
    d[((d >> np.uint32(23)) & np.uint32(0xFF) == np.uint32(0xFF)) & ((d & np.uint32(0x7FFFFF)) != 0)] = np.uint32(
        0x7FC00000
    )
    return bytes(a)


def fnv_c(b):
    """FNV-1a 64 of bytes, via numpy-free pure python for small, chunked C-speed for big."""
    global _FNV_LIB
    if _FNV_LIB is None:
        import ctypes

        so = B / "fnv.dylib"
        if not so.exists():
            (B / "fnv.c").write_text(
                "#include <stdint.h>\n#include <stddef.h>\nuint64_t fnv(const uint8_t*p,size_t n){"
                "uint64_t h=1469598103934665603ull;for(size_t i=0;i<n;i++){h^=p[i];h*=1099511628211ull;}return h;}\n"
            )
            subprocess.run(["clang", "-O2", "-shared", "-o", str(so), str(B / "fnv.c")], check=True)
        _FNV_LIB = ctypes.CDLL(str(so))
        _FNV_LIB.fnv.restype = ctypes.c_uint64
        _FNV_LIB.fnv.argtypes = [ctypes.c_char_p, ctypes.c_size_t]
    b = canon(b)
    return _FNV_LIB.fnv(b, len(b))
