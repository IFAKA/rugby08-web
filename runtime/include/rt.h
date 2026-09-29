/* rt.h -- runtime core shared by the Win32/DirectX HLE and the platform glue.
 * Design: runtime/DESIGN.md. Guest = the recompiled game (32-bit VAs); host = us. */
#ifndef RT_H
#define RT_H
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "recomp/recomp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- guest memory ---------------------------------------------------- */
#define RT_IMAGE_BASE     0x00400000u
#define RT_IMAGE_END      0x01465000u
#define RT_STACK0_LO      0x00100000u   /* main thread stack [LO, HI) */
#define RT_STACK0_HI      0x00300000u
#define RT_TIB0_VA        0x00310000u   /* main thread TIB page */
#define RT_HEAP_LO        0x02000000u
#define RT_BRIDGE_IMPORT  0xFFF00000u   /* IAT slot i  -> RT_BRIDGE_IMPORT + 16*i */
#define RT_BRIDGE_HOST    0xFFE00000u   /* host bridges (COM methods, callbacks) -> + 16*i */

extern uint32_t rt_guest_size;              /* bytes of guest VA space backed */
static inline void    *G2H(uint32_t va)     { return (void *)ADDR(va); }
static inline uint32_t H2G(const void *p)   { return (uint32_t)((uintptr_t)p - (uintptr_t)g_mem_base); }
static inline uint32_t rd32(uint32_t va)    { return MEM32(va); }
static inline void     wr32(uint32_t va, uint32_t v) { MEM32(va) = v; }
static inline uint16_t rd16(uint32_t va)    { return MEM16(va); }
static inline void     wr16(uint32_t va, uint16_t v) { MEM16(va) = v; }
static inline uint8_t  rd8(uint32_t va)     { return MEM8(va); }
static inline void     wr8(uint32_t va, uint8_t v)   { MEM8(va) = v; }
static inline const char *gstr(uint32_t va) { return va ? (const char *)G2H(va) : NULL; }

/* guest heap (dlmalloc mspace over [RT_HEAP_LO, rt_guest_size)); thread-safe */
uint32_t gmalloc(uint32_t size);
uint32_t gcalloc(uint32_t size);
uint32_t grealloc(uint32_t va, uint32_t size);
void     gfree(uint32_t va);
uint32_t gmsize(uint32_t va);
uint32_t gmemalign(uint32_t align, uint32_t size);
uint32_t gstrdup(const char *s);

/* ---- calls ----------------------------------------------------------- */
/* Call guest function `va` (stdcall or cdecl) with n 32-bit args; returns eax.
 * Callee-saved registers and esp are restored afterwards, so cdecl works too. */
uint32_t guest_call(uint32_t va, int n, ...);
uint32_t guest_callv(uint32_t va, int n, const uint32_t *args);

/* Import bridges (generated table, gen/runtime/imports.c) */
typedef uint32_t (*rt_hle_fn)(const uint32_t *a);
typedef struct { uint32_t iat_va; const char *dll, *name; uint16_t pop, slots; rt_hle_fn fn; } rt_import_t;
extern const rt_import_t rt_imports[];
extern const uint32_t rt_import_count;

/* Host bridges: a guest-callable VA backed by a host function with the same handler ABI.
 * pop = stdcall bytes popped (4*argc; 0 for cdecl). Returns the VA. */
uint32_t rt_host_bridge(const char *name, rt_hle_fn fn, uint16_t pop);

/* COM: allocate a guest vtable of n host bridges and an object {vtbl, host_ptr}. */
typedef struct { const char *name; rt_hle_fn fn; uint16_t pop; } rt_method_t;
uint32_t rt_com_vtable(const char *iface, const rt_method_t *m, int n);
uint32_t rt_com_new(uint32_t vtbl, void *host);          /* returns guest `this` */
void    *rt_com_host(uint32_t this_va);                  /* host pointer stored at this+4 */

/* ---- thread state ---------------------------------------------------- */
/* Initialise this host thread's CPU state for running guest code on stack [lo,hi). */
void rt_thread_enter(uint32_t stack_lo, uint32_t stack_hi, uint32_t tib_va);
uint32_t rt_thread_tib(void);
extern RECOMP_TLS uint32_t rt_last_error;

/* ---- diagnostics ----------------------------------------------------- */
/* rt_log: line to logs/<run>.log (native) or console (web). rt_trace: also stdout, for checks. */
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rt_trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rt_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
extern int rt_verbose;

/* ---- host paths ------------------------------------------------------- */
extern char rt_game_dir[1024];     /* host path of "Game Files" */
extern char rt_exe_path[1024];     /* host path of Rugby08_unpacked.exe */

#ifdef __cplusplus
}
#endif
#endif
