/* recomp_fp.h -- the x87 stack of a lifted body: in locals where the body uses it, else the globals.
 * A push or pop on the global stack shifts eight thread-local doubles; in a local array with constant
 * indices the compiler keeps them in registers. tools/lift/lift_passes.fp_locals puts RECOMP_FP_LOCALS
 * in each body that touches the stack; it syncs with g_st / g_fp_top where the integer registers do
 * (RECOMP_REGS_OUT / RECOMP_REGS_IN: calls, returns, tail jumps). Prior art: box64's x87 cache and
 * FEX-Emu's x87 stack optimisation pass keep the stack in host registers within a block.
 * Elsewhere the file-scope stand-ins below are never touched (l_fp is 0: the sync is dead code). */
#if defined(RECOMP_LOCAL_REGS)
static const int l_fp = 0;
static double l_st[8];
static int l_fp_top;
#define RECOMP_FP_LOCALS                                                                                               \
    const int l_fp = 1;                                                                                                \
    double l_st[8];                                                                                                    \
    int l_fp_top;                                                                                                      \
    RECOMP_FP_IN();
#define RECOMP_FP_OUT() (l_fp ? (__builtin_memcpy(g_st, l_st, sizeof l_st), g_fp_top = l_fp_top) : 0)
#define RECOMP_FP_IN()  (l_fp ? (__builtin_memcpy(l_st, g_st, sizeof l_st), l_fp_top = g_fp_top) : 0)
#define _st             l_st
#define _fp_top         l_fp_top
#else
#define RECOMP_FP_LOCALS
#define RECOMP_FP_OUT() 0
#define RECOMP_FP_IN()  0
#define _st             g_st
#define _fp_top         g_fp_top
#endif
