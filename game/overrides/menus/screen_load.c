/* sub_004EBAE0 -- the screen registry's load (thiscall on FE+0x31414, the screen id): loads every screen
 * of the id's package from data.gob and makes the id's the current one. Replaced for the party's own
 * screens (ids FE_SCR_*, not in the registry): their XML (runtime/core/fe_screens.c) goes through the
 * same file loader (0x5A06D0) into the same list. */
#include "game.h"
#include "fe_party.h"
#include <stdlib.h>
#include <string.h>

#define FE_PTR 0x013834E8u

GAME_OVERRIDE(sub_004EBAE0) {
    int id = (int)(GAME_ARG(0) & 0xFFFF);
    size_t len;
    char *x = fe_party_screen_xml(id, &len);
    if (!x) {
        sub_004EBAE0_lifted();
        return;
    }
    uint32_t fe = rd32(FE_PTR), buf = gcalloc((uint32_t)len + 1);
    if (buf) {
        memcpy(G2H(buf), x, len + 1);
        guest_call(0x005A06D0u, 5, buf, 0u, rd32(fe + 0x2FB14), fe + 4, (uint32_t)len);
        gfree(buf);
        for (uint32_t n = rd32(fe + 0x2FB14), h = n; n; n = rd32(n + 0x10)) {   /* FE+4: the loaded screen */
            if (rd16(n + 4) == id && rd32(n + 8)) {
                wr32(fe + 4, rd32(n + 8));
                break;
            }
            if (rd32(n + 0x10) == h) break;
        }
    }
    free(x);
    GAME_RETURN(0, 4);
}
