/* aems.h -- EA AEMS sound-bank code lifted ahead of time (tools/lift/aems_lift.py -> gen/aems/aems.c).
 * The game copies each bank's resident image to the heap; its code was lifted at a canonical
 * address (aems_bank_t.canon), and aems_lookup maps heap or canonical addresses to it. */
#ifndef AEMS_H
#define AEMS_H
#include "recomp/recomp_types.h"

typedef struct {
    const char *name;
    uint32_t canon, res, code_lo, code_hi;      /* image offsets: code is [code_lo, code_hi) */
    const uint8_t *img, *mask;                  /* file bytes of the code range; mask = fixup bytes */
    const recomp_dispatch_entry_t *fns;         /* lifted functions, canonical addresses, sorted */
    uint32_t nfn;
} aems_bank_t;

extern const aems_bank_t aems_banks[];
extern const uint32_t aems_bank_count;
extern uint32_t aems_base[];                    /* runtime load base per bank, 0 = not seen yet */

recomp_func_t aems_lookup(uint32_t va);         /* runtime/core/aems.c; NULL if not AEMS code */
#endif
