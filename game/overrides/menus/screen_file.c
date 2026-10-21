/* sub_005A06D0 -- the front end's screen file loader (cdecl: data, 0, the loaded-screen list, &FE+4,
 * size): parses one .rfe screen (XML text or the game's binary XML) into an FeScreen. Replaced so the
 * main menu (screen 50) gets the party's ONLINE item (runtime/core/fe_screens.c): its binary file is
 * read into a tree, edited and handed on as XML text. RT_FE_DUMP=DIR also writes every screen file the
 * game parses there (fe_N.rfe, in load order), to study them. */
#include "game.h"
#include "fe_party.h"
#include "fe_xml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void dump(uint32_t data, uint32_t size) {
    static const char *dir;
    static int n, said;
    if (!said++) dir = getenv("RT_FE_DUMP");
    if (!dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s/fe_%03d.rfe", dir, n++);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(G2H(data), 1, size, f);
    fclose(f);
}

GAME_OVERRIDE(sub_005A06D0) {
    uint32_t data = GAME_ARG(0), size = GAME_ARG(4);
    if (!data || !size) {
        sub_005A06D0_lifted();
        return;
    }
    dump(data, size);
    fe_node *s = fe_online() ? fe_parse_bin(G2H(data), size) : NULL;
    const char *id = s ? fe_get(s, "ID") : NULL;
    if (!id || strcmp(id, "50") != 0) {
        fe_free(s);
        sub_005A06D0_lifted();
        return;
    }
    fe_menu_edit(s);
    size_t len;
    char *x = fe_to_xml(s, &len);
    fe_free(s);
    uint32_t buf = x ? gcalloc((uint32_t)len + 1) : 0;
    if (!buf) {
        free(x);
        sub_005A06D0_lifted();
        return;
    }
    memcpy(G2H(buf), x, len + 1);
    free(x);
    wr32(g_esp + 4, buf);   /* the arguments the original reads (cdecl: the caller's) */
    wr32(g_esp + 20, (uint32_t)len);
    sub_005A06D0_lifted();
    gfree(buf);
}
