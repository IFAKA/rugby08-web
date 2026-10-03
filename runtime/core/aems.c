/* aems.c -- dispatch into AEMS sound-bank code (runtime/include/aems.h).
 * A bank's base is found on the first dispatch miss into it: base = va - entry offset for some
 * lifted entry, accepted when the whole code range matches the bank file outside fixup bytes. */
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
        if (a < canon_va) lo = mid + 1; else hi = mid;
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

recomp_func_t aems_lookup(uint32_t va) {
    for (uint32_t i = 0; i < aems_bank_count; i++) {
        const aems_bank_t *b = &aems_banks[i];
        if (va - b->canon < b->res) return find(b, va);
        if (aems_base[i] && va - aems_base[i] < b->res) return find(b, b->canon + (va - aems_base[i]));
    }
    recomp_func_t f = NULL;
    pthread_mutex_lock(&g_mu);
    for (uint32_t i = 0; i < aems_bank_count && !f; i++) {
        const aems_bank_t *b = &aems_banks[i];
        if (aems_base[i]) continue;
        for (uint32_t k = 0; k < b->nfn; k++) {
            uint32_t base = va - (b->fns[k].address - b->canon);
            if (matches(b, base)) {
                aems_base[i] = base;
                rt_log("AEMS: %s loaded at 0x%08X (entered at +0x%X)", b->name, base, va - base);
                f = b->fns[k].func;
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_mu);
    return f;
}
