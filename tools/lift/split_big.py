#!/usr/bin/env python3
"""Split the giant lifted functions of gen/recomp/*.c into parts, for the web build.

V8's optimizing wasm tier (TurboFan) needs about 1 KB of compiler memory per byte of function body
and the renderer dies with "V8 process OOM (Zone)" when a hot function is large: the lifted game has
functions of up to 850 KB. A lifted body is a flat list of `L_<va>:` labels with gotos between them,
and the registers/flags are already flushed to globals around calls (RECOMP_REGS_OUT/RECOMP_FLAGS_OUT),
so cutting at a label is cheap: a jump to a label in another part flushes the state and tail-calls
that part with an entry id (musttail, so nothing grows the stack). The three function-scope locals
that are not registers or flags (_fpu_cmp, _df, _itail_tgt) become thread-local globals.

usage: split_big.py SRC_DIR DST_DIR [--min-lines N] [--part-lines N]
Only files holding a function above --min-lines are rewritten (written to DST_DIR); others are not
copied (the build keeps using SRC_DIR for them). One summary line; details in logs/split_big.log.
"""

import os
import re
import sys

args = sys.argv[1:]


def opt(name, default):
    if name in args:
        i = args.index(name)
        v = int(args[i + 1])
        del args[i : i + 2]
        return v
    return default


MIN = opt("--min-lines", 3000)
PART = opt("--part-lines", 1000)
ONLY = set(os.environ.get("SPLIT_ONLY", "").split(",")) - {""}  # debugging: split just these functions
src, dst = args[0], args[1]
os.makedirs(dst, exist_ok=True)

FN = re.compile(r"^(static )?void (sub_[0-9A-Fa-f]+(?:_eh)?)\(void\) \{$")
LABEL = re.compile(r"^(L_[0-9A-Fa-f]+):")
ANYLABEL = re.compile(r"(?:^|[;{}]\s*|^\s+)(L_[0-9A-Fa-f]+):")
GOTO = re.compile(r"\bgoto (L_[0-9A-Fa-f]+|_ljump);")
LOCALS = {"_fpu_cmp": "g_sp_fpu", "_df": "g_sp_df", "_itail_tgt": "g_sp_itail"}
LOCAL_RE = re.compile(r"\b(" + "|".join(LOCALS) + r")\b")

PRELUDE = """
/* split_big.py: parts of a split function pass control with SPJ (flush state, tail call, entry id) */
static RECOMP_TLS uint32_t g_sp_entry;
static RECOMP_TLS int g_sp_fpu, g_sp_df;
static RECOMP_TLS uint32_t g_sp_itail;
#ifdef SPJ_TRACE\n#include <stdio.h>\n#define SPJ_LOG(part, id) fprintf(stderr, "SPJ %s %u eax=%x ebp=%x esp=%x\\n", #part, (unsigned)(id), g_eax, g_ebp, g_esp)\n#else\n#define SPJ_LOG(part, id) ((void)0)\n#endif\n#define SPJ(part, id) do { RECOMP_REGS_OUT(); SPJ_LOG(part, id); RECOMP_FLAGS_OUT(); g_sp_entry = (id); __attribute__((musttail)) return part(); } while (0)
"""


def split_function(name, static, lines):
    """lines: the function from its `void name(void) {` line through its closing `}`."""
    # head: declarations through RECOMP_ENTER
    he = next(i for i, l in enumerate(lines) if "RECOMP_ENTER(" in l)
    head_decl = lines[1:he]  # declarations before RECOMP_ENTER
    enter = lines[he]
    body = lines[he + 1 : -1]
    lj = next((i for i, l in enumerate(body) if l.strip() == "_ljump:"), len(body))
    main, ljump = body[:lj], body[lj:]
    # blocks at label lines, brace depth 0
    blocks, cur, depth = [], [], 0
    for l in main:
        if depth == 0 and LABEL.match(l) and cur:
            blocks.append(cur)
            cur = []
        cur.append(l)
        depth += l.count("{") - l.count("}")
    if cur:
        blocks.append(cur)
    parts, cur, n = [], [], 0
    for b in blocks:
        if cur and n >= PART:
            parts.append(cur)
            cur = []
            n = 0
        cur += b
        n += len(b)
    if cur:
        parts.append(cur)
    if ljump:
        parts.append(ljump)  # the jump-table dispatcher is its own part
    if len(parts) < 2:
        return None
    pname = lambda k: name if k == 0 else f"{name}_p{k}"
    label_part = {}
    for k, p in enumerate(parts):
        for l in p:
            for m in ANYLABEL.finditer(l):
                label_part[m.group(1)] = k
        if k == len(parts) - 1 and ljump:
            label_part["_ljump"] = k
    ids, entries = {}, [dict() for _ in parts]  # entries[k][label] = id

    def jump(_frm, tgt):
        k = label_part[tgt]
        if tgt not in ids:
            ids[tgt] = len(ids) + 1
        entries[k][tgt] = ids[tgt]
        return f"SPJ({pname(k)}, {ids[tgt]});"

    out_parts = []
    CASE = re.compile(r"^(\s*case 0x[0-9A-Fa-f]+u:) goto (L_[0-9A-Fa-f]+);\s*$")
    for k, p in enumerate(parts):
        txt, used, is_lj = [], set(), bool(ljump) and k == len(parts) - 1
        sw_depth = None  # brace depth inside the dispatcher's `switch (_itail_tgt) {`, once it opens
        for l in p:
            m = CASE.match(l) if is_lj else None
            if m:  # the jump-table dispatcher: pick (part, entry) here, jump once after the switch
                jump(k, m.group(2))
                used.add(label_part[m.group(2)])
                txt.append(f"{m.group(1)} _spp = {label_part[m.group(2)]}; g_sp_entry = {ids[m.group(2)]}; break;")
                continue
            if is_lj and sw_depth is None and l.lstrip().startswith("switch ("):
                sw_depth = 0
            if is_lj and sw_depth is not None and sw_depth >= 0:
                sw_depth += l.count("{") - l.count("}")
                if sw_depth == 0 and "}" in l:
                    # the switch closed: a rewritten case `break`s to here, so the jump goes right here.
                    # (Labels after the switch, the function's exits to outside VAs, must not see it.)
                    txt.append(l)
                    txt.append("    switch (_spp) {")
                    for kk in sorted(used):
                        txt.append(f"      case {kk}: SPJ({pname(kk)}, g_sp_entry);")
                    txt.append("    }")
                    sw_depth = -1
                    continue
            l = LOCAL_RE.sub(lambda m: LOCALS[m.group(1)], l)

            def rep(m):
                t = m.group(1)
                return m.group(0) if label_part[t] == k else "{ " + jump(k, t) + " }"

            txt.append(GOTO.sub(rep, l))
        if k + 1 < len(parts):  # fall through into the next part
            nxt = parts[k + 1]
            first = (
                "_ljump"
                if (ljump and k + 1 == len(parts) - 1)
                else next(LABEL.match(l).group(1) for l in nxt if LABEL.match(l))
            )
            txt.append("    { " + jump(k, first) + " }")
        out_parts.append(txt)
    decls = [l for l in head_decl if not any(re.search(r"\b" + v + r"\b", l) for v in LOCALS)]
    decls_rest = [l for l in decls if "RECOMP_EH_BODY_ENTER" not in l]  # the EH frame is entered once, by part 0
    res = [f"static void {pname(k)}(void);" for k in range(1, len(parts))]
    for k, txt in enumerate(out_parts):
        res.append(("static " if (k > 0 or static) else "") + f"void {pname(k)}(void) {{")
        res += decls if k == 0 else decls_rest
        if bool(ljump) and k == len(parts) - 1:
            res.append("    int _spp = 0;")
        sw = []
        if entries[k]:  # entered from another part: continue at the label (a plain call has g_sp_entry == 0)
            sw = ["    { uint32_t _e = g_sp_entry; g_sp_entry = 0;", "    switch (_e) {"]
            for t, i in sorted(entries[k].items(), key=lambda x: x[1]):
                sw.append(f"      case {i}: goto {t};")
            sw.append("      default: break;" if k == 0 else "      default: __builtin_unreachable();")
            sw += ["    } }"]
        res += sw
        if k == 0:
            res.append("    g_sp_fpu = 3; g_sp_df = 1; g_sp_itail = 0;")
            res.append(enter)
        res += txt
        res.append("}")
    return res, len(parts)


total = changed = 0
keep = set()
log = open("logs/split_big.log", "w")
for fn in sorted(os.listdir(src)):
    if not fn.endswith(".c"):
        continue
    lines = open(os.path.join(src, fn), errors="surrogateescape").read().split("\n")
    out, i, did = [], 0, False
    while i < len(lines):
        m = FN.match(lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue
        j = i + 1
        while lines[j] != "}":
            j += 1
        func = lines[i : j + 1]
        r = (
            split_function(m.group(2), bool(m.group(1)), func)
            if len(func) > MIN and (not ONLY or m.group(2) in ONLY)
            else None
        )
        if r:
            out += r[0]
            did = True
            changed += 1
            log.write(f"{fn} {m.group(2)} {len(func)} lines -> {r[1]} parts\n")
        else:
            out += func
        i = j + 1
    total += 1
    if did:
        k = out.index('#include "recomp_funcs.h"') + 1
        out.insert(k, PRELUDE)
        new, path = "\n".join(out), os.path.join(dst, fn)
        keep.add(fn)
        if not (
            os.path.exists(path) and open(path, errors="surrogateescape").read() == new
        ):  # keep mtimes: no needless recompiles
            open(path, "w", errors="surrogateescape").write(new)
for old in os.listdir(dst):
    if old.endswith(".c") and old not in keep:
        os.remove(os.path.join(dst, old))
print(f"split_big: {changed} functions above {MIN} lines split into parts of ~{PART} lines ({total} files scanned)")
