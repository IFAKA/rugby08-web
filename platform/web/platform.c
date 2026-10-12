/* platform/web/platform.c -- browser host glue. Window, events and input: platform/sdl/. */
#include "rt.h"
#include "plat.h"
#include <emscripten.h>
#include <stdlib.h>

void rt_platform_init(int argc, char **argv) {
    (void)argc;
    (void)argv;
}

/* The browser presents the canvas when control returns to it: count the frame for the page's fps
 * counter (shell Module.rtFrames), then yield (JSPI) so the browser can present, deliver input and run
 * timers, and resume the game loop. */
void plat_gl_swap(void) {
    EM_ASM(Module.rtFrames = (Module.rtFrames | 0) + 1);
    emscripten_sleep(0);
}

/* wasm32: guest VAs wrap mod 2^32 through ADDR(); the first 1 GiB is backed. */
void *plat_guest_arena(uint32_t *size) {
    *size = 0x40000000u;
    void *arena = calloc(1, *size);
    if (!arena) rt_fatal("arena: cannot allocate %u bytes", *size);
    return arena;
}
