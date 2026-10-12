/* platform/native/platform.c -- macOS host glue: the guest arena, the fault handler. Window, events and
 * input: platform/sdl/; the swap and frame pacing: frame.c; GL shader dialect: gl_native.c. */
#include "rt.h"
#include "plat.h"
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
/* A host fault inside lifted code is a guest access violation: name the guest address, the lifted
 * function and the guest stack, dump this thread's entry ring, and stop. */
static void on_fault(int sig, siginfo_t *si, void *ctx) {
    (void)ctx;
    uintptr_t a = (uintptr_t)si->si_addr, b = (uintptr_t)g_mem_base;
    rt_log("FAULT: signal %d at host %p (guest 0x%08llX) in sub_%08X, esp=0x%08X eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X", sig, si->si_addr,
           (unsigned long long)(a - b), g_cur_func, g_esp, g_eax, g_ebx, g_ecx, g_edx, g_esi, g_edi, g_ebp);
    recomp_dump_trace("fault");
    _exit(139);
}
static void install_fault_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

void rt_platform_init(int argc, char **argv) {
    (void)argc;
    (void)argv;
    install_fault_handler();
}

/* The whole 32-bit space is reserved, so any guest VA (even a wild one) is backed; the exe is not
 * LARGEADDRESSAWARE, so user space is the low 2 GiB. Windows never maps the first 64 KiB: a guest null
 * dereference faults there (FAULT in the log, with the lifted function) instead of reading zeros. */
void *plat_guest_arena(uint32_t *size) {
    *size = 0x80000000u;
    void *arena = mmap(NULL, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (arena == MAP_FAILED) rt_fatal("arena: mmap 4 GiB failed");
    mprotect(arena, 0x10000, PROT_NONE);
    return arena;
}
