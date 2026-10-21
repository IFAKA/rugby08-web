/* k32_eh.c -- guest structured exceptions: RaiseException, RtlUnwind, and the landing-pad jump.
 *
 * The guest's own exception machinery is lifted and faithful: frames link through fs:[0] in guest
 * memory, and MSVC's __CxxFrameHandler (with its FuncInfo tables, unwind funclets and catch blocks)
 * is ordinary lifted code. So RaiseException only does what ntdll's dispatcher does -- call each
 * handler on the chain until one takes the exception -- and RtlUnwind calls the handlers above the
 * target frame with EXCEPTION_UNWINDING and unlinks them.
 *
 * The one step the host must do itself is the end of a catch: _JumpToContinuation restores the
 * catching frame's ebp/esp and jumps to the continuation the catch block returned. That jump is
 * RECOMP_ITAIL; while a dispatch is active it calls rt_eh_jump, which longjmps to the landing pad of
 * the lifted function that installed that frame (RECOMP_EH_WRAP, tools/lift/lift.py eh_wrap), discarding
 * the host frames of the throw, and the body resumes at the continuation's label. */
#include "win32.h"
#include <stdlib.h>
#include <string.h>

RECOMP_TLS rt_ehpad_t *g_eh_top;

extern const uint32_t recomp_eh_cont[];      /* gen/recomp/recomp_dispatch.c, sorted */
extern const uint32_t recomp_eh_cont_count;

#define EXCEPTION_NONCONTINUABLE   0x01u
#define EXCEPTION_UNWINDING        0x02u
#define EXCEPTION_EXIT_UNWIND      0x04u
#define ExceptionContinueExecution 0u
#define CHAIN_END                  0xFFFFFFFFu

static int is_continuation(uint32_t va) {
    size_t lo = 0, hi = recomp_eh_cont_count;
    while (lo < hi) {
        size_t m = (lo + hi) / 2;
        if (recomp_eh_cont[m] < va) lo = m + 1;
        else hi = m;
    }
    return lo < recomp_eh_cont_count && recomp_eh_cont[lo] == va;
}

/* Only a catch continuation diverts, and only to the frame ebp names: unwind funclets run with the
 * same ebp and tail-jump to destructors, which must stay ordinary tail calls. */
void rt_eh_jump(uint32_t va) {
    if (!is_continuation(va)) return;
    for (rt_ehpad_t *p = g_eh_top; p; p = p->prev) {
        if (p->ebp && p->ebp == g_ebp) {
            g_eh_active = 0;
            p->resume = va;
            g_eh_top = p;
            RT_LONGJMP(p->jb, 1);
        }
    }
    rt_log("EH: continuation 0x%08X for ebp 0x%08X has no landing pad", va, g_ebp);
}

static uint32_t fs0(void) { return rd32(g_fs_base); }

/* EXCEPTION_RECORD (0x50) and CONTEXT (0x2CC) for the handlers, below the guest stack. */
static uint32_t make_record(uint32_t below, uint32_t code, uint32_t flags, uint32_t nargs, uint32_t args,
                            uint32_t *ctx) {
    uint32_t rec = (below - 0x400u) & ~15u;
    memset(G2H(rec), 0, 0x50);
    wr32(rec + 0, code);
    wr32(rec + 4, flags);
    if (nargs > 15) nargs = 15;
    wr32(rec + 16, nargs);
    for (uint32_t i = 0; i < nargs; i++) wr32(rec + 20 + 4 * i, rd32(args + 4 * i));
    *ctx = rec + 0x60;
    memset(G2H(*ctx), 0, 0x2CC);
    wr32(*ctx + 0, 0x10007u);               /* CONTEXT_FULL */
    wr32(*ctx + 0x9C, g_edi);
    wr32(*ctx + 0xA0, g_esi);
    wr32(*ctx + 0xA4, g_ebx);
    wr32(*ctx + 0xA8, g_edx);
    wr32(*ctx + 0xAC, g_ecx);
    wr32(*ctx + 0xB0, g_eax);
    wr32(*ctx + 0xB4, g_ebp);
    wr32(*ctx + 0xC4, g_esp);
    return rec;
}

static uint32_t call_handler(uint32_t node, uint32_t rec, uint32_t ctx, uint32_t disp) {
    return guest_call(rd32(node + 4), 4, rec, node, ctx, disp);
}

HLE(RaiseException) {
    uint32_t code = a[0], flags = a[1] & EXCEPTION_NONCONTINUABLE, nargs = a[2], args = a[3];
    if (code == 0x406D1388u) {
        /* MS_VC_EXCEPTION: SetThreadName for an attached debugger. The raiser wraps it in
         * __try/__except(EXCEPTION_EXECUTE_HANDLER) and does nothing in the handler, so without a
         * debugger it is a no-op. THREADNAME_INFO: dwType(0x1000) szName dwThreadID dwFlags. */
        if (nargs >= 2 && rd32(args) == 0x1000u && rd32(args + 4))
            rt_log("SetThreadName(0x%X, \"%s\")", rd32(args + 8), gstr(rd32(args + 4)));
        return 0;
    }
    uint32_t sv_esp = g_esp, ctx;
    uint32_t rec = make_record(g_esp, code, flags, nargs, args, &ctx);
    uint32_t disp = rec - 4;
    g_esp = rec - 16;
    g_eh_active++;
    for (uint32_t node = fs0(); node && node != CHAIN_END; node = rd32(node)) {
        if (call_handler(node, rec, ctx, disp) == ExceptionContinueExecution && !(flags & EXCEPTION_NONCONTINUABLE)) {
            g_eh_active--;
            g_esp = sv_esp;
            return 0;
        }
    }
    g_eh_active--;
    g_esp = sv_esp;
    if (code == 0xE06D7363u && nargs >= 3) {
        uint32_t tinfo = rd32(args + 8), ctarr = tinfo ? rd32(tinfo + 12) : 0;
        uint32_t ct = ctarr ? rd32(ctarr + 4) : 0, td = ct ? rd32(ct + 4) : 0;
        rt_fatal("unhandled C++ exception (type %s) from sub_%08X", td ? gstr(td + 8) : "?", g_cur_func);
    }
    rt_fatal("unhandled exception 0x%08X from sub_%08X", code, g_cur_func);
}

/* RtlUnwind(TargetFrame, TargetIp, ExceptionRecord, ReturnValue): every frame above TargetFrame gets
 * its handler called with EXCEPTION_UNWINDING (C++ frames run their destructors) and is unlinked. */
HLE(RtlUnwind) {
    uint32_t target = a[0], rec = a[2], ctx;
    uint32_t sv_esp = g_esp;
    uint32_t own = make_record(g_esp, 0xC0000027u /* STATUS_UNWIND */, 0, 0, 0, &ctx);
    if (!rec) rec = own;
    wr32(rec + 4, rd32(rec + 4) | EXCEPTION_UNWINDING | (target ? 0 : EXCEPTION_EXIT_UNWIND));
    g_esp = own - 16;
    for (uint32_t node = fs0(); node && node != CHAIN_END && node != target; node = fs0()) {
        call_handler(node, rec, ctx, own - 4);
        wr32(g_fs_base, rd32(node));
    }
    g_esp = sv_esp;
    return a[3];
}
