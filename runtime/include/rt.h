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
void rt_guest_yield(void);   /* RT_BIGLOCK: hand the guest lock to a waiting thread (cpu.c) */

/* Import bridges (generated table, gen/runtime/imports.c) */
typedef uint32_t (*rt_hle_fn)(const uint32_t *a);
/* Define an import handler: HLE(CreateFileA) { ... a[0] ... return value; }  (a[k] = k-th arg slot).
 * tools/gen_imports.py finds these by name; an import without one logs UNIMPL and returns 0. */
#define HLE(name) uint32_t hle_##name(const uint32_t *a)
typedef struct { uint32_t iat_va; const char *dll, *name; uint16_t pop, slots; rt_hle_fn fn; } rt_import_t;
extern const rt_import_t rt_imports[];
extern const uint32_t rt_import_count;

/* Host bridges: a guest-callable VA backed by a host function with the same handler ABI.
 * pop = stdcall bytes popped (4*argc; 0 for cdecl). Returns the VA. */
uint32_t rt_host_bridge(const char *name, rt_hle_fn fn, uint16_t pop);

/* COM: allocate a guest vtable of n host bridges and an object {vtbl, host_ptr}. */
typedef struct { const char *name; rt_hle_fn fn; uint16_t pop; } rt_method_t;
uint32_t rt_com_vtable(const char *iface, const rt_method_t *m, int n);
void rt_host_bridge_keep_lock(uint32_t bridge_va);   /* the call never blocks: it keeps the guest lock */
uint32_t rt_com_new(uint32_t vtbl, void *host);          /* returns guest `this` */
void    *rt_com_host(uint32_t this_va);                  /* host pointer stored at this+4 */
void     rt_com_free(uint32_t this_va);

/* ---- thread state ---------------------------------------------------- */
/* Initialise this host thread's CPU state for running guest code on stack [lo,hi). */
void rt_thread_enter(uint32_t stack_lo, uint32_t stack_hi, uint32_t tib_va);
uint32_t rt_thread_tib(void);

/* ---- deterministic mode (det.c) ---------------------------------------- */
extern int rt_det;
int det_on(void);                     /* RT_DET=1 or a netplay session */
uint64_t det_now_ns(void);            /* virtual time */
uint64_t det_clock_read(void);        /* a guest clock read: virtual time, spin-aware */
int det_thread_id(void);              /* -1: a host thread */
typedef int (*det_ready_fn)(void *arg);
typedef void (*det_frame_fn)(uint32_t frame);
void det_init_main(void);
void det_yield(void);
void det_preempt(void);              /* a time slice of guest work ended */
int  det_wait(det_ready_fn ready, void *arg, uint64_t deadline_ns);   /* 1 ready, 0 timeout */
void det_sleep_ms(uint32_t ms);
int  det_thread_new(det_ready_fn ready, void *arg);
void det_thread_begin(int id);
void det_thread_end(void);
void det_set_deadline_fn(uint64_t (*fn)(void));   /* extra wake-up time (timers) */
void det_on_frame(det_frame_fn fn);   /* called first at each det_frame, in registration order */
void det_frame(uint32_t frame);       /* Present */
uint64_t det_hash(void);
int det_hash_step(uint32_t frame, uint32_t parts, uint32_t every, uint64_t *out);   /* incremental: 1 + hash of the last `every` slices */
void rt_thread_tls_set_all(uint32_t slot, uint32_t v);   /* TLS slot of every thread (TlsFree clears it) */
extern RECOMP_TLS uint32_t rt_last_error;

/* ---- diagnostics ----------------------------------------------------- */
/* rt_log: line to logs/<run>.log (native) or console (web). rt_trace: also stdout, for checks. */
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rt_trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rt_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
extern int rt_verbose;

/* ---- checks ----------------------------------------------------------- */
extern int rt_frame_limit;              /* --frames N (<= 0: run until the game exits) */
extern char rt_screenshot_path[1024];   /* --screenshot PATH ("" = none) */
extern char rt_audio_capture_path[1024]; /* --audio-capture PATH ("" = none) */
extern int rt_log_input;                 /* --log-input: log host key/mouse events and what the game polls */
extern char rt_shot_path[1024];          /* replay `<frame> shot PATH`: PNG of that frame (then "") */
extern char rt_replay_path[1024];        /* --replay PATH ("" = none) */
extern int rt_replay_live;               /* --replay-live: replay without the automated-run isolation */
extern volatile uint32_t rt_present_frame;  /* presented frames so far (d3d8 Present) */
/* tools/hooks.json "shadow:NAME": run the function in the interpreter first (writes undone), then
 * compare the lifted run's registers and memory against it (runtime/core/interp.c). */
void rt_shadow_enter(uint32_t va, const char *name);
void rt_shadow_auto(uint32_t va);
uint32_t rt_enter_mark(void);                       /* log.c entry ring (RECOMP_TRACE builds) */
int rt_enter_since(uint32_t mark, uint32_t *out, int max);   /* RT_SHADOW_ALL=from-to: every function entered (recomp_trace_enter) */
/* PNG (RGBA8, rows top to bottom); 0 = ok. */
int rt_png_write(const char *path, int w, int h, const uint8_t *rgba);

/* ---- host paths ------------------------------------------------------- */
extern char rt_game_dir[1024];     /* host path of "Game Files" */
extern char rt_exe_path[1024];     /* host path of Rugby08_unpacked.exe */

#ifdef __cplusplus
}
#endif
#endif
