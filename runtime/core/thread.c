/* thread.c -- per-thread guest state: stack, TIB (fs:), CPU registers. */
#include "rt.h"
#include <string.h>

static RECOMP_TLS uint32_t t_tib;

/* NT_TIB / TEB fields the MSVC CRT and SEH prologues touch through fs: */
#define TIB_EXCEPTION_LIST 0x00
#define TIB_STACK_BASE     0x04
#define TIB_STACK_LIMIT    0x08
#define TIB_SELF           0x18
#define TEB_PID            0x20
#define TEB_TID            0x24
#define TEB_TLS_POINTER    0x2C
#define TEB_PEB            0x30
#define TEB_LAST_ERROR     0x34

void rt_thread_enter(uint32_t stack_lo, uint32_t stack_hi, uint32_t tib_va) {
    memset(G2H(tib_va), 0, 0x1000);
    wr32(tib_va + TIB_EXCEPTION_LIST, 0xFFFFFFFFu);
    wr32(tib_va + TIB_STACK_BASE, stack_hi);
    wr32(tib_va + TIB_STACK_LIMIT, stack_lo);
    wr32(tib_va + TIB_SELF, tib_va);
    wr32(tib_va + TEB_PID, 0x1000);
    wr32(tib_va + TEB_TID, tib_va >> 12);
    wr32(tib_va + TEB_TLS_POINTER, tib_va + 0xE10);   /* TlsSlots live in the TEB page */
    t_tib = tib_va;
    g_fs_base = tib_va;
    g_esp = stack_hi - 16;
    g_ebp = 0;
    g_eax = g_ebx = g_ecx = g_edx = g_esi = g_edi = 0;
    g_fp_top = 0;
    g_fpu_cw = 0x027F;
}

uint32_t rt_thread_tib(void) { return t_tib; }
