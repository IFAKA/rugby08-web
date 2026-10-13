/* join_view.c -- joining a party shows that party's screen from the start (runtime/core/ui_badge.c). Once
 * the server says the party can be joined, this game shows its party screen (plat_join_preview), the page
 * keeps a picture of it across the reload into the room (plat_join_snapshot) and gives it back while the
 * game catches up (plat_join_backdrop). The page's side: platform/web/shell.html (Module.rugby*). */
#include "plat.h"
#include <emscripten.h>

// clang-format off
EM_JS(int, js_join_preview, (int *slot, uint32_t *players, char *code, int cap), {
    var p = Module.rugbyPreview;
    if (!p) return 0;
    HEAP32[slot >> 2] = p.slot;
    HEAPU32[players >> 2] = p.players >>> 0;
    stringToUTF8(p.room, code, cap);
    return 1;
});
EM_JS(void, js_join_snapshot, (void), { if (Module.rugbySnapshot) Module.rugbySnapshot(); });
/* the picture, scaled to W x H, into OUT (RGBA): 0 none (or not decoded yet) */
EM_JS(int, js_join_backdrop, (uint8_t *out, int w, int h), {
    var img = Module.rugbyBackdrop;
    if (!img || !img.complete || !img.naturalWidth) return 0;
    var c = document.createElement('canvas');
    c.width = w;
    c.height = h;
    var g = c.getContext('2d');
    g.drawImage(img, 0, 0, w, h);
    HEAPU8.set(g.getImageData(0, 0, w, h).data, out);
    return 1;
});
// clang-format on

int plat_join_preview(int *slot, uint32_t *players, char *code, int cap) {
    return js_join_preview(slot, players, code, cap);
}
void plat_join_snapshot(void) { js_join_snapshot(); }
int plat_join_backdrop(uint8_t *rgba, int w, int h) { return js_join_backdrop(rgba, w, h); }
