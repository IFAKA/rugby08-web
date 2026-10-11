/* difftest_harness.c -- runs lifted leaf functions from a case file; one result line per case.
 * Built by tools/difftest.py together with the generated build/difftest/funcs.c.
 * Memory model identical to the runtime: 4 GiB reserved arena at g_mem_base, zero-filled.
 * Built with -DDT_INTERP (tools/difftest.py --interp), DT_MODE picks how each case runs:
 *   0 native  the lifted body;
 *   1 interp  runtime/core/interp.c on the function's bytes, lifted bodies hidden;
 *   2 thunk   interp.c on `pop [save]; call f; push [save]; ret`: the interpreter hands off to the
 *             lifted f at its entry and resumes after the call (registers and flags pass through). */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
#include <setjmp.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

RECOMP_TLS uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
RECOMP_TLS double   g_st[8];
RECOMP_TLS int      g_fp_top;
RECOMP_TLS uint16_t g_fpu_cw = 0x027F;
RECOMP_TLS uint32_t g_flag_k, g_flag_a, g_flag_b, g_flag_cf;
RECOMP_TLS uint32_t g_fs_base;
RECOMP_TLS uint64_t g_mm[8];
RECOMP_TLS uint32_t g_cur_func;
uint16_t g_seg_cs, g_seg_ds, g_seg_es, g_seg_fs, g_seg_gs, g_seg_ss;
uint32_t g_gs_base;
ptrdiff_t g_mem_base;
uint32_t g_icall_trace[ICALL_TRACE_SIZE], g_icall_from[ICALL_TRACE_SIZE];
uint32_t g_icall_trace_idx, g_icall_count;
const recomp_dispatch_entry_t recomp_dispatch_table[1];
const uint32_t recomp_dispatch_count = 0;
typedef void (*fn_t)(void);
extern const fn_t dt_funcs[];
#ifdef DT_INTERP
extern const uint32_t dt_vas[];
extern const unsigned dt_n;
static int g_hide;                                  /* DT_MODE 1: every body is "not lifted" */
uint32_t rt_guest_size = 0xFFFF0000u;               /* interp.c maps the whole arena */
uint32_t gcalloc(uint32_t size) { (void)size; return 0x2FFD0000u; }   /* the interpreter's GDT */
recomp_func_t recomp_lookup(uint32_t va) {
    if (g_hide) return 0;
    for (unsigned i = 0; i < dt_n; i++) if (dt_vas[i] == va) return dt_funcs[i];
    return 0;
}
#else
int rt_interp(uint32_t va) { (void)va; return 0; }
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return 0; }
#endif
recomp_func_t recomp_lookup_manual(uint32_t va) { (void)va; return 0; }
recomp_func_t recomp_lookup_import(uint32_t va) { (void)va; return 0; }
uint64_t recomp_host_rdtsc(void) { return 0; }
RECOMP_TLS rt_ehpad_t *g_eh_top;
RECOMP_TLS int g_eh_active;
RECOMP_TLS uint32_t g_eh_resume;
void rt_eh_jump(uint32_t va) { (void)va; }

static sigjmp_buf g_case_env;
static volatile int g_status;
/* a computed jump (a ret popping something other than RET_VA): the reference runs on into the
   target, the harness has no other functions -- not comparable (difftest.py excludes status 5) */
void recomp_miss(int tail, uint32_t va, uint32_t from) { (void)tail; (void)va; (void)from; g_status = 5; }
void rt_fatal(const char *fmt, ...) {          /* the message goes to stderr; difftest.py reads stdout */
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr);
    g_status = 3; siglongjmp(g_case_env, 1);
}
void rt_log(const char *fmt, ...) { (void)fmt; g_status = 4; }   /* soft fault: reference raised #DE */
void rt_trace(const char *fmt, ...) { (void)fmt; }                 /* interp.c's shadow check (unused here) */
volatile uint32_t rt_present_frame;
static void on_signal(int sig) { g_status = sig == SIGALRM ? 2 : 1; siglongjmp(g_case_env, 1); }

#define IMG_LO   0x00400000u
#define DATA_LO  0x008CC000u
#define IMG_HI   0x01465000u
#define SCR_LO   0x20000000u
#define SCR_SZ   0x00040000u
#define STK_LO   0x10000000u
#define STK_SZ   0x00010000u
#define OUT_VA   0x2FFF0000u
#define RET_VA   0x7FFE0000u
#define THUNK_VA 0x2FFE0000u
#define SAVE_VA  0x2FFE0100u

#ifdef DT_INTERP
static void run_interp(int mode, unsigned idx) {
    uint32_t entry = dt_vas[idx];
    if (mode == 2) {                                /* pop [save]; call f; push [save]; ret */
        uint8_t *t = (uint8_t *)ADDR(THUNK_VA);
        uint32_t rel = entry - (THUNK_VA + 11);
        t[0] = 0x8F; t[1] = 0x05; memcpy(t + 2, &(uint32_t){SAVE_VA}, 4);
        t[6] = 0xE8; memcpy(t + 7, &rel, 4);
        t[11] = 0xFF; t[12] = 0x35; memcpy(t + 13, &(uint32_t){SAVE_VA}, 4);
        t[17] = 0xC3;
        entry = THUNK_VA;
    }
    g_hide = mode == 1;
    if (!rt_interp(entry)) g_status = 3;
    g_hide = 0;
}
#endif

/* NaN payload/sign are outside the model (runtime/DESIGN.md): canonicalise before hashing, the
 * same way tools/difftest.py does -- 8-aligned double NaNs first, then 4-aligned float NaNs. */
static void canon(uint32_t va, uint32_t n) {
    uint64_t *q = (uint64_t *)ADDR(va);
    for (uint32_t i = 0; i < n / 8; i++)
        if (((q[i] >> 52) & 0x7FF) == 0x7FF && (q[i] & 0xFFFFFFFFFFFFFull)) q[i] = 0x7FF8000000000000ull;
    uint32_t *d = (uint32_t *)ADDR(va);
    for (uint32_t i = 0; i < n / 4; i++)
        if (((d[i] >> 23) & 0xFF) == 0xFF && (d[i] & 0x7FFFFF)) d[i] = 0x7FC00000u;
}

static uint64_t fnv(uint32_t va, uint32_t n) {
    canon(va, n);
    const uint8_t *p = (const uint8_t *)ADDR(va);
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

static uint8_t *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n);
    if (fread(b, 1, *n, f) != *n) exit(2);
    fclose(f);
    return b;
}

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: harness image.bin scratch.bin cases.txt\n"); return 2; }
    int mode = getenv("DT_MODE") ? atoi(getenv("DT_MODE")) : 0;
    void *arena = mmap(NULL, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (arena == MAP_FAILED) { perror("mmap"); return 2; }
    g_mem_base = (ptrdiff_t)arena;
    size_t img_n, scr_n;
    uint8_t *img = slurp(argv[1], &img_n), *scr = slurp(argv[2], &scr_n);
    FILE *cf = fopen(argv[3], "r");
    if (!cf) { perror(argv[3]); return 2; }
    struct sigaction sa = {0};
    sa.sa_handler = on_signal;
    sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0); sigaction(SIGALRM, &sa, 0);
    sigaction(SIGFPE, &sa, 0); sigaction(SIGILL, &sa, 0);

    char line[4096];
    while (fgets(line, sizeof line, cf)) {
        unsigned idx, r[8], args[8], pre, npages, pages[64];
        char *p = line; int used;
        if (sscanf(p, "%u%n", &idx, &used) != 1) continue; p += used;
        for (int i = 0; i < 8; i++) { sscanf(p, "%x%n", &r[i], &used); p += used; }
        for (int i = 0; i < 8; i++) { sscanf(p, "%x%n", &args[i], &used); p += used; }
        sscanf(p, "%u%n", &pre, &used); p += used;
        sscanf(p, "%u%n", &npages, &used); p += used;
        if (npages > 64) npages = 64;
        for (unsigned i = 0; i < npages; i++) { sscanf(p, "%x%n", &pages[i], &used); p += used; }

        memcpy((void *)ADDR(IMG_LO), img, img_n);
        memcpy((void *)ADDR(SCR_LO), scr, scr_n);
        memset((void *)ADDR(STK_LO), 0, STK_SZ);
        memset((void *)ADDR(OUT_VA), 0, 4096);
        for (unsigned i = 0; i < npages; i++) memset((void *)ADDR(pages[i]), 0, 4096);

        g_eax = r[0]; g_ecx = r[1]; g_edx = r[2]; g_ebx = r[3];
        g_esp = r[4]; g_ebp = r[5]; g_esi = r[6]; g_edi = r[7];
        for (int i = 7; i >= 0; i--) { g_esp -= 4; MEM32(g_esp) = args[i]; }
        g_esp -= 4; MEM32(g_esp) = RET_VA;
        memset(g_st, 0, sizeof g_st);
        if (pre) { g_st[0] = -2.25; g_st[1] = 1.5; }
        g_fp_top = (int)pre; g_fpu_cw = 0x027F;   /* as tools/difftest.py */
        MEMD(OUT_VA + 0x10) = 1.5; MEMD(OUT_VA + 0x18) = -2.25;
        g_flag_k = FK_EFLAGS; g_flag_a = 0x202; g_flag_b = 0; g_flag_cf = 0;
        g_status = 0;
        if (sigsetjmp(g_case_env, 1) == 0) {
            alarm(2);
#ifdef DT_INTERP
            if (mode) run_interp(mode, idx); else
#endif
            dt_funcs[idx]();
            alarm(0);
        } else {
            alarm(0);
        }
        /* The model's depth is an unbounded counter; hardware TOP is it modulo 8 (code that
         * ffree's instead of popping wraps it -- sub_007BA1B9). Compare modulo 8. */
        if (g_status == 0 && (g_fp_top & 7)) MEMD(OUT_VA) = g_st[0];
        const char *dump = getenv("DT_DUMP_IDX");
        if (dump && (unsigned)atoi(dump) == idx) {
            FILE *df = fopen("build/difftest/dump_c.bin", "wb");
            fwrite((void *)ADDR(SCR_LO), 1, SCR_SZ, df); fwrite((void *)ADDR(STK_LO), 1, STK_SZ, df); fclose(df);
        }
        uint64_t hx = 1469598103934665603ull;
        for (unsigned i = 0; i < npages; i++) hx ^= fnv(pages[i], 4096) + i;
        /* The live stack (final esp to the top) after the whole stack: DT_MODE 2's thunk leaves other
           values than a direct call in dead slots below esp (callee-popped args), so it compares this. */
        uint64_t hstk = fnv(STK_LO, STK_SZ);
        uint32_t live = g_esp >= STK_LO && g_esp <= STK_LO + STK_SZ ? g_esp : STK_LO;
        uint64_t hlive = fnv(live, STK_LO + STK_SZ - live);
        printf("%u %d %08X %08X %08X %08X %08X %08X %08X %08X %d %016llX %016llX %016llX %016llX %016llX %016llX\n",
               idx, g_status, g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi, g_fp_top & 7,
               (unsigned long long)fnv(DATA_LO, IMG_HI - DATA_LO), (unsigned long long)fnv(SCR_LO, SCR_SZ),
               (unsigned long long)hstk, (unsigned long long)hx,
               (unsigned long long)fnv(OUT_VA, 8), (unsigned long long)hlive);
        fflush(stdout);
    }
    return 0;
}
