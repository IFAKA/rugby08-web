#!/usr/bin/env python3
"""Write build/deobf/Rugby08_deobf.exe: Rugby08_unpacked.exe with the 933 obfuscated calls
rewritten (tools/code_patch.py) so analysers (Ghidra) see real call/jmp instead of push/push/ret.
The original exe is never modified. Prints one line."""
import pathlib, sys
import pefile
root = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / "tools"))
import code_patch
pe = pefile.PE(str(root / "Rugby08_unpacked.exe"))
text = [s for s in pe.sections if s.Name.rstrip(b"\0") == b".text"][0]
base = pe.OPTIONAL_HEADER.ImageBase
lo = base + text.VirtualAddress
raw = bytearray(open(root / "Rugby08_unpacked.exe", "rb").read())
off, size = text.PointerToRawData, min(text.SizeOfRawData, text.Misc_VirtualSize)
patched, sites = code_patch.patch(bytes(raw[off:off + size]), lo, lo + size)
raw[off:off + size] = patched
out = root / "build/deobf/Rugby08_deobf.exe"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(raw)
print(f"patch_exe: {len(sites)} obfuscated calls rewritten -> {out.relative_to(root)}")
