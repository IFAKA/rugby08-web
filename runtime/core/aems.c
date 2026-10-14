/* aems.c -- dispatch into AEMS sound-bank code (runtime/include/aems.h).
 * A bank's base is found on a dispatch miss into it: base = va - entry offset for some lifted entry,
 * accepted when the whole code range matches the bank file outside fixup bytes. The game unloads and
 * loads banks again (leaving a match reloads the menus' fe_sfx.abk elsewhere): a bank seen at a new
 * address moves there. Banks with the same code (the stadium loops) each keep one live copy: a bank
 * whose copy is still in place is not moved while another one fits. */
#include "rt.h"
#include "aems.h"
#include <pthread.h>
#include <string.h>

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static recomp_func_t find(const aems_bank_t *b, uint32_t canon_va) {
    uint32_t lo = 0, hi = b->nfn;
    while (lo < hi) {
        uint32_t mid = (lo + hi) >> 1, a = b->fns[mid].address;
        if (a == canon_va) return b->fns[mid].func;
        if (a < canon_va) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

static int matches(const aems_bank_t *b, uint32_t base) {
    uint32_t lo = base + b->code_lo, n = b->code_hi - b->code_lo;
    if (lo < 0x10000u || (uint64_t)lo + n > rt_guest_size) return 0;
    const uint8_t *m = G2H(lo);
    for (uint32_t k = 0; k < n; k++)
        if (!b->mask[k] && m[k] != b->img[k]) return 0;
    return 1;
}

/* the bytes at entry VA are B's code at BASE (cheap: the first 8, fixups aside) */
static int entry_ok(const aems_bank_t *b, uint32_t base, uint32_t va) {
    const uint8_t *m = G2H(va);
    for (uint32_t o = va - base, k = 0; k < 8; k++, o++)
        if (o >= b->code_lo && o < b->code_hi && !b->mask[o - b->code_lo] && m[k] != b->img[o - b->code_lo]) return 0;
    return 1;
}

/* the entry of bank I at BASE that VA is, or NULL */
static recomp_func_t entry_at(uint32_t i, uint32_t base, uint32_t va) {
    const aems_bank_t *b = &aems_banks[i];
    recomp_func_t f = find(b, b->canon + (va - base));
    return f && matches(b, base) ? f : NULL;
}

recomp_func_t aems_lookup(uint32_t va) {
    for (uint32_t i = 0; i < aems_bank_count; i++) {
        const aems_bank_t *b = &aems_banks[i];
        if (va - b->canon < b->res) return find(b, va);
        if (aems_base[i] && va - aems_base[i] < b->res && entry_ok(b, aems_base[i], va)) {
            recomp_func_t f = find(b, b->canon + (va - aems_base[i]));
            if (f) return f;
        }
    }
    recomp_func_t f = NULL;
    pthread_mutex_lock(&g_mu);
    /* first a bank not placed yet or whose copy is gone, then any (the oldest copy gives way) */
    for (int pass = 0; pass < 2 && !f; pass++)
        for (uint32_t i = 0; i < aems_bank_count && !f; i++) {
            const aems_bank_t *b = &aems_banks[i];
            if (!pass && aems_base[i] && matches(b, aems_base[i])) continue;
            for (uint32_t k = 0; k < b->nfn && !f; k++) {
                uint32_t base = va - (b->fns[k].address - b->canon);
                if ((f = entry_at(i, base, va)) != NULL) {
                    rt_log("AEMS: %s %s at 0x%08X (entered at +0x%X)", b->name, aems_base[i] ? "moved" : "loaded", base,
                           va - base);
                    aems_base[i] = base;
                }
            }
        }
    pthread_mutex_unlock(&g_mu);
    return f;
}
