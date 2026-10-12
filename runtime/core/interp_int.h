/* interp_int.h -- the interpreter internals runtime/debug/shadow.c shares (runtime/core/interp.c). */
#ifndef RT_INTERP_INT_H
#define RT_INTERP_INT_H
#include "rt.h"
#include <string.h>
#include <unicorn/unicorn.h>

enum { STOP_NONE, STOP_NATIVE, STOP_BRIDGE, STOP_FAULT };
#define RING 32

typedef struct { uint64_t m; uint16_t e; } f80_t;   /* Unicorn's FPn layout: mantissa, then exponent */
extern RECOMP_TLS int t_stop;
extern RECOMP_TLS uint32_t t_fault_va, t_fault_kind;
extern RECOMP_TLS uint32_t t_ring[RING];
extern RECOMP_TLS unsigned t_ring_i;

static inline uint32_t rd(uint32_t va) { uint32_t v; memcpy(&v, G2H(va), 4); return v; }
static inline void wr(uint32_t va, uint32_t v) { memcpy(G2H(va), &v, 4); }
recomp_func_t native_at(uint32_t va);
bool hook_unmapped(uc_engine *uc, uc_mem_type type, uint64_t addr, int size, int64_t v, void *ud);
void hook_intr(uc_engine *uc, uint32_t intno, void *ud);
int hook_cpuid(uc_engine *uc, void *ud);
void state_in(uc_engine *uc);    /* guest registers, flags and x87 stack -> engine */
void state_out(uc_engine *uc);   /* engine -> guest state */
uc_engine *interp_engine_new(const char *who, uc_cb_hookcode_t block_hook, uc_cb_hookmem_t write_hook);

#endif
