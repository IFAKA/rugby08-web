/* mem.c -- guest arena reservation and PE image load (all sections at their VAs). */
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef __EMSCRIPTEN__
#include <sys/mman.h>
#endif

uint32_t rt_guest_size;
void rt_heap_init(uint32_t hi);

void rt_mem_init(void) {
#ifdef __EMSCRIPTEN__
    /* wasm32: guest VAs wrap mod 2^32 through ADDR(); back the first 1 GiB. */
    rt_guest_size = 0x40000000u;
    void *arena = calloc(1, rt_guest_size);
    if (!arena) rt_fatal("arena: cannot allocate %u bytes", rt_guest_size);
#else
    /* Reserve the whole 32-bit space so any guest VA (even a wild one) is backed. */
    rt_guest_size = 0x80000000u;   /* the exe is not LARGEADDRESSAWARE: user space < 2 GiB */
    void *arena = mmap(NULL, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (arena == MAP_FAILED) rt_fatal("arena: mmap 4 GiB failed");
    /* Windows never maps the first 64 KiB: a guest null dereference faults there (FAULT in the
     * log, with the lifted function) instead of reading zeros and running on. */
    mprotect(arena, 0x10000, PROT_NONE);
#endif
    g_mem_base = (ptrdiff_t)arena;
    rt_heap_init(rt_guest_size - 0x01000000u);   /* top 16 MiB kept free (thread TIBs etc.) */
}

/* Minimal PE32 loader: headers + every section's raw data at ImageBase + VirtualAddress. */
uint32_t rt_load_image(const char *path, uint32_t *entry_out) {
    FILE *f = fopen(path, "rb");
    if (!f) rt_fatal("cannot open %s", path);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) rt_fatal("cannot read %s", path);
    fclose(f);
    uint32_t pe = *(uint32_t *)(b + 0x3C);
    if (memcmp(b + pe, "PE\0\0", 4)) rt_fatal("%s: not a PE file", path);
    uint16_t nsec = *(uint16_t *)(b + pe + 6), opt = *(uint16_t *)(b + pe + 20);
    uint8_t *oh = b + pe + 24;
    uint32_t entry = *(uint32_t *)(oh + 16), base = *(uint32_t *)(oh + 28);
    uint32_t hdr_size = *(uint32_t *)(oh + 60);
    if (base != RT_IMAGE_BASE) rt_fatal("unexpected image base 0x%08X", base);
    memcpy(G2H(base), b, hdr_size);
    uint8_t *sh = oh + opt;
    for (int i = 0; i < nsec; i++, sh += 40) {
        uint32_t vsize = *(uint32_t *)(sh + 8), va = *(uint32_t *)(sh + 12);
        uint32_t rsize = *(uint32_t *)(sh + 16), roff = *(uint32_t *)(sh + 20);
        uint32_t cp = rsize < vsize ? rsize : vsize;
        if (roff + cp > (uint32_t)n) cp = (uint32_t)n - roff;
        memcpy(G2H(base + va), b + roff, cp);
        rt_log("image: section %.8s VA 0x%08X size 0x%X", (const char *)sh, base + va, vsize);
    }
    free(b);
    *entry_out = base + entry;
    return base;
}

/* Point every IAT slot at its bridge VA (gen/runtime/imports.c order = bridge index). */
void rt_bind_imports(void) {
    for (uint32_t i = 0; i < rt_import_count; i++)
        wr32(rt_imports[i].iat_va, RT_BRIDGE_IMPORT + 16 * i);
    rt_log("imports: %u IAT slots bound", rt_import_count);
}
