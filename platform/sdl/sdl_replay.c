/* sdl_replay.c -- input replay (--replay FILE, tests/replays/): scripted keys, clicks, mouse motion,
 * pads and screenshots, injected at Present as ordinary input events. */
#include "sdl_int.h"

typedef struct {
    uint32_t frame;
    uint8_t down, mouse;
    SDL_Scancode sc;
    int x, y;
    char *path;
} replay_ev_t;   /* mouse 3: shot */
static replay_ev_t *g_replay;
static int g_nreplay, g_ireplay, g_replay_loaded;

/* Room for `extra` more events (the script is read once; running out of memory there is fatal). */
static void replay_room(int extra) {
    replay_ev_t *p = realloc(g_replay, (size_t)(g_nreplay + extra) * sizeof *g_replay);
    if (!p) rt_fatal("replay: out of memory");
    g_replay = p;
}

static void replay_add(uint32_t frame, int down, SDL_Scancode sc) {
    replay_room(1);
    g_replay[g_nreplay++] = (replay_ev_t){ frame, (uint8_t)down, 0, sc, 0, 0 };
}

static int replay_cmp(const void *a, const void *b) {
    const replay_ev_t *x = a, *y = b;
    return x->frame != y->frame ? (x->frame < y->frame ? -1 : 1) : 0;
}

static void replay_load(void) {
    g_replay_loaded = 1;
    if (!rt_replay_path[0]) return;
    FILE *f = fopen(rt_replay_path, "r");
    if (!f) {
        rt_log("replay: cannot open %s", rt_replay_path);
        return;
    }
    char line[256], act[16], key[64];
    unsigned frame;
    int mx, my;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%u click %d %d", &frame, &mx, &my) == 3) {   /* left click at window x,y */
            replay_room(2);
            g_replay[g_nreplay++] = (replay_ev_t){ frame, 1, 1, SDL_SCANCODE_UNKNOWN, mx, my };
            g_replay[g_nreplay++] = (replay_ev_t){ frame + 3, 0, 1, SDL_SCANCODE_UNKNOWN, mx, my };
            continue;
        }
        mx = 0;
        unsigned hold = 3;                                                 /* `press [frames held]` */
        if (sscanf(line, "%u press%n %u", &frame, &mx, &hold) >= 1 && mx > 0) { /* left click wherever the cursor is */
            replay_room(2);
            g_replay[g_nreplay++] = (replay_ev_t){ frame, 1, 1, SDL_SCANCODE_UNKNOWN, -1, -1 };
            g_replay[g_nreplay++] = (replay_ev_t){ frame + (hold ? hold : 3), 0, 1, SDL_SCANCODE_UNKNOWN, -1, -1 };
            continue;
        }
        int pad;
        char pname[32];
        if (sscanf(line, "%u pad %d %15s %31s", &frame, &pad, act, pname) == 4) { /* virtual pad (plat.h rt_pad_t) */
            int tap = !strcmp(act, "tap");
            replay_room(2);
            g_replay[g_nreplay++] =
                (replay_ev_t){ frame, (uint8_t)strcmp(act, "up"), 4, SDL_SCANCODE_UNKNOWN, pad, 0, strdup(pname) };
            if (tap)
                g_replay[g_nreplay++] = (replay_ev_t){ frame + 3, 0, 4, SDL_SCANCODE_UNKNOWN, pad, 0, strdup(pname) };
            continue;
        }
        char shot[512];
        if (sscanf(line, "%u shot %511s", &frame, shot) == 2) { /* PNG of that presented frame */
            replay_room(1);
            g_replay[g_nreplay++] = (replay_ev_t){ frame, 0, 3, SDL_SCANCODE_UNKNOWN, 0, 0, strdup(shot) };
            continue;
        }
        if (sscanf(line, "%u move %d %d", &frame, &mx, &my) == 3) { /* relative mouse motion (DirectInput) */
            replay_room(1);
            g_replay[g_nreplay++] = (replay_ev_t){ frame, 0, 2, SDL_SCANCODE_UNKNOWN, mx, my };
            continue;
        }
        if (line[0] == '#' || sscanf(line, "%u %15s %63[^\n]", &frame, act, key) != 3) continue;
        SDL_Scancode sc = SDL_GetScancodeFromName(key);
        if (sc == SDL_SCANCODE_UNKNOWN) {
            rt_log("replay: unknown key '%s'", key);
            continue;
        }
        if (!strcmp(act, "tap")) {
            replay_add(frame, 1, sc);
            replay_add(frame + 3, 0, sc);
        } /* held 3 frames */
        else if (!strcmp(act, "down"))
            replay_add(frame, 1, sc);
        else if (!strcmp(act, "up")) replay_add(frame, 0, sc);
    }
    fclose(f);
    qsort(g_replay, (size_t)g_nreplay, sizeof *g_replay, replay_cmp);
    rt_log("replay: %d events from %s", g_nreplay, rt_replay_path);
}

static uint32_t g_replay_last;
uint32_t plat_replay_last_event(void) { return g_replay_last; }

/* like a real mouse: DirectInput delta, cursor position and WM_MOUSEMOVE */
static void replay_move(const replay_ev_t *r, uint32_t frame) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    int wx = 0, wy = 0, ww = 640, wh = 480;
    if (g_win) {
        SDL_GetWindowPosition(g_win, &wx, &wy);
        SDL_GetWindowSize(g_win, &ww, &wh);
    }
    int cx = g_vcur_x - wx + r->x, cy = g_vcur_y - wy + r->y;
    cx = cx < 0 ? 0 : cx > ww - 1 ? ww - 1 : cx;
    cy = cy < 0 ? 0 : cy > wh - 1 ? wh - 1 : cy;
    g_vcur_x = wx + cx;
    g_vcur_y = wy + cy;
    e.type = SDL_MOUSEMOTION;
    e.motion.windowID = g_win ? SDL_GetWindowID(g_win) : 0;
    e.motion.x = cx;
    e.motion.y = cy;
    e.motion.xrel = r->x;
    e.motion.yrel = r->y;
    if (g_inited) translate(&e);
    rt_trace("replay: frame %u mouse move %d,%d", frame, r->x, r->y);
}

/* a left button at a window position (x < 0: wherever the virtual cursor is) */
static void replay_click(const replay_ev_t *r, uint32_t frame) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    int wx = 0, wy = 0;
    if (g_win) SDL_GetWindowPosition(g_win, &wx, &wy);
    if (r->x >= 0) {
        g_vcur_x = wx + r->x;
        g_vcur_y = wy + r->y;
    }
    if (rt_replay_live && g_win && r->x >= 0)
        SDL_WarpMouseInWindow(g_win, r->x, r->y); /* live: the real pointer goes there */
    int bx = g_vcur_x - wx, by = g_vcur_y - wy;
    e.type = r->down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
    e.button.windowID = g_win ? SDL_GetWindowID(g_win) : 0;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.state = r->down ? SDL_PRESSED : SDL_RELEASED;
    e.button.clicks = 1;
    e.button.x = bx;
    e.button.y = by;
    if (g_inited) translate(&e); /* direct: real input is filtered out in automated runs */
    g_replay_buttons = r->down ? 1 : 0;
    rt_trace("replay: frame %u mouse %s at %d,%d", frame, r->down ? "down" : "up", bx, by);
}

static void replay_key(const replay_ev_t *r, uint32_t frame) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = r->down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.state = r->down ? SDL_PRESSED : SDL_RELEASED;
    e.key.windowID = g_win ? SDL_GetWindowID(g_win) : 0;
    e.key.keysym.scancode = r->sc;
    e.key.keysym.sym = SDL_GetKeyFromScancode(r->sc);
    if (g_inited) translate(&e); /* direct: real input is filtered out in automated runs */
    keymap_t k = g_keys[r->sc]; /* DirectInput state now, not when the pump runs */
    if (k.scan) g_dik_down[(k.scan | (k.ext ? 0x80 : 0)) & 0xFF] = r->down;
    rt_trace("replay: frame %u %s %s", frame, r->down ? "down" : "up", SDL_GetScancodeName(r->sc));
}

void plat_replay_tick(uint32_t frame) {
    if (!g_replay_loaded) replay_load();
    for (; g_ireplay < g_nreplay && g_replay[g_ireplay].frame <= frame; g_ireplay++) {
        const replay_ev_t *r = &g_replay[g_ireplay];
        g_replay_last = frame;
        switch (r->mouse) { /* the event kind: 0 key, 1 click, 2 move, 3 shot, 4 pad */
        case 3: snprintf(rt_shot_path, sizeof rt_shot_path, "%s", r->path); break;
        case 4:
            if (!rt_pad_event(r->x, r->path, r->down)) rt_log("replay: unknown pad input '%s'", r->path);
            rt_trace("replay: frame %u pad %d %s %s", frame, r->x, r->down ? "down" : "up", r->path);
            break;
        case 2: replay_move(r, frame); break;
        case 0: replay_key(r, frame); break;
        default: replay_click(r, frame);
        }
    }
}
