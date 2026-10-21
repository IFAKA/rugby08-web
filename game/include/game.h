/* game.h -- writing a game function in C instead of its lifted body (game/overrides/README.md).
 *
 *   GAME_OVERRIDE(sub_0044A7B0) {          // CTackleDrill::vf1 (tools/query/fn.sh CTackleDrill::vf1)
 *       uint32_t self = g_ecx;              // thiscall: `this` in ecx
 *       if (GAME_ARG(0) == 0) GAME_RETURN(0, 4);
 *       sub_0044A7B0_lifted();              // otherwise the original
 *   }
 *
 * The lifter (tools/lift/lift.py) emits an overridden function's body as sub_X_lifted; re-run it after
 * adding or removing an override. An override runs exactly where the lifted body would: the guest
 * registers are in the g_* globals, [esp] is the return address, the arguments follow it. */
#ifndef GAME_H
#define GAME_H
#include "recomp_types.h"
#include "rt.h"

#define GAME_OVERRIDE(fn)                                                                                              \
    void fn##_lifted(void);                                                                                            \
    void fn(void)
#define GAME_ARG(n) rd32(g_esp + 4u + 4u * (uint32_t)(n))   /* stack argument n (0 = first) */
/* return v in eax; pop = the bytes of arguments a stdcall/thiscall callee pops (0 for cdecl) */
#define GAME_RETURN(v, pop)                                                                                            \
    do {                                                                                                               \
        g_eax = (uint32_t)(v);                                                                                         \
        g_esp += 4u + (pop);                                                                                           \
        return;                                                                                                        \
    } while (0)

/* A range inside a lifted function in C: GAME_BLOCK(00774300, 007743A2) { ... } runs instead of the
 * lifted code from 0x774300 and continues at 0x7743A2 (tools/lift/lift_passes.py game_blocks). It sees
 * the guest registers, flags and x87 stack in the g_* globals and leaves them as that code would. */
#define GAME_BLOCK(start, end) void block_##start(void)

#endif
