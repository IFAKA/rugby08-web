/* k32_mem.c -- Heap, Virtual, Global/Local alloc and IsBad*Ptr over the guest heap. */
#include "win32.h"
#include <string.h>

/* HeapAlloc blocks carry their requested size in an 8-byte header (HeapSize returns it
 * exactly, as Windows does); the pointer handed out stays 8-aligned. */
#define HDR 8u
#define HEAP_ZERO_MEMORY 0x08u
#define HEAP_REALLOC_IN_PLACE_ONLY 0x10u

static uint32_t heap_alloc(uint32_t size, int zero) {
    uint32_t va = zero ? gcalloc(size + HDR) : gmalloc(size + HDR);
    if (!va) return set_err(ERROR_NOT_ENOUGH_MEMORY_);
    wr32(va, size);
    wr32(va + 4, 0x48454150u);   /* "HEAP" tag, to spot foreign frees */
    return va + HDR;
}

static int heap_owned(uint32_t p) { return p >= RT_HEAP_LO + HDR && rd32(p - 4) == 0x48454150u; }

static void heap_free(uint32_t p) {
    if (!p) return;
    if (!heap_owned(p)) { rt_log("HeapFree: foreign pointer 0x%08X", p); return; }
    wr32(p - 4, 0);
    gfree(p - HDR);
}

static uint32_t heap_realloc(uint32_t p, uint32_t size, uint32_t flags) {
    if (!p) return heap_alloc(size, flags & HEAP_ZERO_MEMORY);
    uint32_t old = rd32(p - HDR);
    if (flags & HEAP_REALLOC_IN_PLACE_ONLY) {
        if (size <= gmsize(p - HDR) - HDR) { wr32(p - HDR, size); return p; }
        return set_err(ERROR_NOT_ENOUGH_MEMORY_);
    }
    uint32_t n = grealloc(p - HDR, size + HDR);
    if (!n) return set_err(ERROR_NOT_ENOUGH_MEMORY_);
    wr32(n, size);
    if ((flags & HEAP_ZERO_MEMORY) && size > old) memset(G2H(n + HDR + old), 0, size - old);
    return n + HDR;
}

HLE(GetProcessHeap) { (void)a; static uint32_t h; if (!h) h = h_new(H_HEAP); return h; }
HLE(HeapCreate) { (void)a; return h_new(H_HEAP); }
HLE(HeapDestroy) { h_close(a[0]); return TRUE_; }
HLE(HeapAlloc) { return heap_alloc(a[2], a[1] & HEAP_ZERO_MEMORY); }
HLE(HeapFree) { heap_free(a[2]); return TRUE_; }
HLE(HeapReAlloc) { return heap_realloc(a[2], a[3], a[1]); }
HLE(HeapSize) { return heap_owned(a[2]) ? rd32(a[2] - HDR) : 0xFFFFFFFFu; }

#define GMEM_ZEROINIT 0x40u
HLE(GlobalAlloc) { return heap_alloc(a[1], a[0] & GMEM_ZEROINIT); }
HLE(GlobalFree) { heap_free(a[0]); return 0; }
HLE(LocalFree) { heap_free(a[0]); return 0; }

/* ---- VirtualAlloc: reservations are 64 KiB-aligned blocks from the guest heap -------------- */
#define MEM_COMMIT 0x1000u
#define MEM_RESERVE 0x2000u
#define MEM_DECOMMIT 0x4000u
#define MEM_RELEASE 0x8000u
#define MEM_FREE 0x10000u
#define MEM_PRIVATE 0x20000u
#define MEM_IMAGE 0x1000000u
#define PAGE_READWRITE 0x04u

typedef struct { uint32_t base, size; } vres_t;
#define MAX_VRES 4096
static vres_t g_vres[MAX_VRES];
static int g_nvres;
static pthread_mutex_t g_vmu = PTHREAD_MUTEX_INITIALIZER;

static vres_t *vres_find(uint32_t addr) {
    for (int i = 0; i < g_nvres; i++)
        if (addr >= g_vres[i].base && addr < g_vres[i].base + g_vres[i].size) return &g_vres[i];
    return NULL;
}

HLE(VirtualAlloc) {
    uint32_t addr = a[0], size = a[1], type = a[2];
    pthread_mutex_lock(&g_vmu);
    if (addr) {
        vres_t *r = vres_find(addr);
        pthread_mutex_unlock(&g_vmu);
        if (r && addr + size <= r->base + r->size) return addr & ~0xFFFu;  /* commit inside a reservation */
        rt_log("VirtualAlloc: fixed address 0x%08X size 0x%X type 0x%X not honoured", addr, size, type);
        return set_err(ERROR_INVALID_PARAMETER_);
    }
    size = (size + 0xFFFFu) & ~0xFFFFu;
    uint32_t va = gmemalign(0x10000u, size);
    if (!va || g_nvres >= MAX_VRES) { pthread_mutex_unlock(&g_vmu); return set_err(ERROR_NOT_ENOUGH_MEMORY_); }
    memset(G2H(va), 0, size);
    g_vres[g_nvres++] = (vres_t){ va, size };
    pthread_mutex_unlock(&g_vmu);
    return va;
}

HLE(VirtualFree) {
    uint32_t addr = a[0], size = a[1], type = a[2];
    pthread_mutex_lock(&g_vmu);
    vres_t *r = vres_find(addr);
    if (!r) { pthread_mutex_unlock(&g_vmu); return set_err(ERROR_INVALID_PARAMETER_); }
    if (type & MEM_RELEASE) {
        gfree(r->base);
        *r = g_vres[--g_nvres];
    } else if (type & MEM_DECOMMIT) {
        uint32_t lo = addr & ~0xFFFu, hi = size ? (addr + size + 0xFFFu) & ~0xFFFu : r->base + r->size;
        if (hi > r->base + r->size) hi = r->base + r->size;
        memset(G2H(lo), 0, hi - lo);   /* a later re-commit must see zero pages */
    }
    pthread_mutex_unlock(&g_vmu);
    return TRUE_;
}

HLE(VirtualProtect) { if (a[3]) wr32(a[3], PAGE_READWRITE); return TRUE_; }

/* MEMORY_BASIC_INFORMATION (28 bytes) */
HLE(VirtualQuery) {
    uint32_t addr = a[0], mbi = a[1], len = a[2];
    if (len < 28) return 0;
    uint32_t base, size, state = MEM_COMMIT, type = MEM_PRIVATE, alloc;
    pthread_mutex_lock(&g_vmu);
    vres_t *r = vres_find(addr);
    if (r) { base = alloc = r->base; size = r->size; }
    else if (addr >= RT_IMAGE_BASE && addr < RT_IMAGE_END) { base = alloc = RT_IMAGE_BASE; size = RT_IMAGE_END - RT_IMAGE_BASE; type = MEM_IMAGE; }
    else if (addr >= rd32(rt_thread_tib() + 8) && addr < rd32(rt_thread_tib() + 4)) {
        alloc = rd32(rt_thread_tib() + 8); base = addr & ~0xFFFu; size = rd32(rt_thread_tib() + 4) - base;
    } else { base = addr & ~0xFFFFu; alloc = base; size = 0x10000; state = MEM_COMMIT; }
    pthread_mutex_unlock(&g_vmu);
    wr32(mbi + 0, base); wr32(mbi + 4, alloc); wr32(mbi + 8, PAGE_READWRITE);
    wr32(mbi + 12, size); wr32(mbi + 16, state); wr32(mbi + 20, PAGE_READWRITE); wr32(mbi + 24, type);
    return 28;
}

static uint32_t bad_ptr(uint32_t p, uint32_t n) { return (n && (p < 0x10000u || p + n < p)) ? TRUE_ : FALSE_; }
HLE(IsBadReadPtr) { return bad_ptr(a[0], a[1]); }
HLE(IsBadWritePtr) { return bad_ptr(a[0], a[1]); }
HLE(IsBadCodePtr) { return a[0] < 0x10000u ? TRUE_ : FALSE_; }
