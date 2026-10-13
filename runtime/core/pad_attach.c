/* pad_attach.c -- the game's controllers follow the pad slots (pt.controllers; docs/PARTY_RESEARCH.md
 * "Controller hot-plug"). The game enumerates DirectInput once, at boot, so every pad slot exists from
 * then on (dinput.c); whether a pad is plugged in is the game's own per-controller flag, CControl+0x2580
 * "attached", which Team Select (FUN_004EA790: icons, sides) and team building (FUN_0062D080) read and
 * only the constructor wrote (from DIDC_ATTACHED). Written here at every Present, as Windows would
 * report a pad that comes and goes: solo shows only the real devices, a friend joining the host's party
 * adds a controller, leaving removes it (off its side first, so nothing waits for it).
 * Team Select and team building only take entries 1..4 of the controller list (FUN_0081A5A0,
 * FUN_0044BA20), the mouse is entry 0: the plugged-in devices are kept in entries 1..4 (keyboard, then
 * plugged pads by slot; with 4 pads plugged the keyboard after them), in both lists the manager keeps
 * (CPCControlMgr +0x18 poll, +0x1C lookup; the side string travels with the controller object). */
#include "rt.h"
#include "plat.h"
#include "pad_attach.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GAME_PTR    0x00A7ED28u
#define G_CTRLMGR   0x10u    /* Game+0x10: CPCControlMgr */
#define M_POLL      0x18u    /* CControl*[9], polled by the manager's thread */
#define M_LOOKUP    0x1Cu    /* CControl*[9], what Team Select and the teams read */
#define C_SIDE      0x2558u  /* char[20]: "H", "A" or "" */
#define C_INDEX     0x256Cu  /* its entry index (also at +0x2578) */
#define C_INDEX2    0x2578u
#define C_TYPE      0x2570u  /* 9 mouse, 8 keyboard, 5 joystick */
#define C_ACTIVE    0x257Cu
#define C_ATTACHED  0x2580u
#define C_DEVICE    0x25E4u  /* its DirectInput device (enumeration index) */
#define T_MOUSE     9u
#define T_KEYBOARD  8u
#define T_PAD       5u
#define MAX_ENTRIES 9
#define SIDE_SLOTS  4        /* entries 1..4 can take a side */

/* A friend's Team Select shows the party (party_sides.c): its keyboard entry stands for the host, pad
   slots 1..3 for the friends, on the sides the host's game has them (0, 'H', 'A'; [4] the keyboard). */
static int g_unplugged;   /* controllers unplugged from a side since rt_pads_take_unplugged */
int rt_pads_take_unplugged(void) {
    int n = g_unplugged;
    g_unplugged = 0;
    return n;
}
static int g_mirror, g_own = -1;          /* g_own: this player's pad slot in the mirror */
static char g_mirror_side[RT_MAX_PADS + 1], g_written[RT_MAX_PADS + 1], g_intent;
/* The side this player moved its own icon to on its own screen (a mouse click on the arrows, ...):
   the host's Team Select must make that move (party_sides.c); 0 none, else 'H', 'A' or 'C' (centre). */
char rt_pads_take_intent(void) {
    char i = g_intent;
    g_intent = 0;
    return i;
}
void rt_pads_mirror_own(int k) { g_own = k; }
void rt_pads_mirror(int on, const char *sides) {
    if (on && !g_mirror) memset(g_written, 1, sizeof g_written);   /* (nothing written yet: no intent) */
    g_mirror = on;
    if (sides) memcpy(g_mirror_side, sides, sizeof g_mirror_side);
}
int rt_pads_mirroring(void) { return g_mirror; }

static char side_of(uint32_t c) {
    char s = (char)rd8(c + C_SIDE);
    return s == 'H' || s == 'A' ? s : 0;
}

/* The side of pad slot k's controller (k = -1: the keyboard), 0 if none or not plugged in. */
char rt_pads_side(int k) {
    uint32_t g = rd32(GAME_PTR), m = g ? rd32(g + G_CTRLMGR) : 0, look = m ? rd32(m + M_LOOKUP) : 0;
    for (int e = 0; look && e < MAX_ENTRIES; e++) {
        uint32_t c = rd32(look + 4u * (uint32_t)e);
        if (!c) break;
        uint32_t t = rd32(c + C_TYPE);
        if ((k < 0 && t == T_KEYBOARD) || (k >= 0 && t == T_PAD && rt_dinput_pad_slot(rd32(c + C_DEVICE)) == k))
            return rd32(c + C_ATTACHED) ? side_of(c) : 0;
    }
    return 0;
}

static void mirror_sides(const uint32_t *c, int n) {
    for (int e = 0; e < n && g_mirror; e++) {
        uint32_t t = rd32(c[e] + C_TYPE);
        int k = t == T_KEYBOARD ? RT_MAX_PADS : t == T_PAD ? rt_dinput_pad_slot(rd32(c[e] + C_DEVICE)) : -1;
        if (k < 0) continue;
        char now = side_of(c[e]), want = rd32(c[e] + C_ATTACHED) ? g_mirror_side[k] : 0;   /* (a leaver's: none) */
        if (k == g_own && g_written[k] != 1 && now != g_written[k]) g_intent = now ? now : 'C';   /* moved here */
        g_written[k] = want;
        if (rd32(c[e] + C_ACTIVE) != (want != 0)) wr32(c[e] + C_ACTIVE, want != 0);
        if (now == want) continue;
        wr8(c[e] + C_SIDE, (uint8_t)want);
        wr8(c[e] + C_SIDE + 1, 0);
    }
}

static void trace(uint32_t frame, const uint32_t *c, int n) {
    static char last[160];
    char s[160];
    int w = 0;
    for (int e = 0; e < n && w < (int)sizeof s - 12; e++) {
        uint32_t t = rd32(c[e] + C_TYPE);
        int k = t == T_PAD ? rt_dinput_pad_slot(rd32(c[e] + C_DEVICE)) : -1;
        char side = (char)rd8(c[e] + C_SIDE), slot[4] = "";
        if (k >= 0) snprintf(slot, sizeof slot, "%d", k);
        w += snprintf(s + w, sizeof s - (size_t)w, "%s%c%s%s%s", e ? " " : "",
                      t == T_MOUSE      ? 'M'
                      : t == T_KEYBOARD ? 'K'
                                        : 'P',
                      slot, rd32(c[e] + C_ATTACHED) ? "" : "-",
                      side == 'H'   ? ":H"
                      : side == 'A' ? ":A"
                                    : "");
    }
    if (!strcmp(s, last)) return;
    strcpy(last, s);
    rt_trace("ms: controllers %s at frame %u", s, frame);
}

typedef struct {
    uint32_t first[MAX_ENTRIES], pads[RT_MAX_PADS], unplugged[MAX_ENTRIES], kb;
    int nf, plugged, nu;
} sorted_t;

/* Plugged in or not, as its slot says; sorts the entries: mouse (and anything else), the keyboard,
   plugged pads by slot, unplugged pads. */
static void plug(const uint32_t *c, int n, sorted_t *o) {
    memset(o, 0, sizeof *o);
    for (int e = 0; e < n; e++) {
        uint32_t t = rd32(c[e] + C_TYPE);
        if (t == T_KEYBOARD) o->kb = c[e];
        else if (t != T_PAD) o->first[o->nf++] = c[e];   /* the mouse keeps its place */
        if (t != T_PAD) continue;
        int k = rt_dinput_pad_slot(rd32(c[e] + C_DEVICE));
        uint32_t want = (uint32_t)rt_pad_attached(k);
        if (rd32(c[e] + C_ATTACHED) != want) {
            if (!want) {   /* unplugged: off its side and inactive first (the game's own disconnect state) */
                g_unplugged += side_of(c[e]) != 0;
                wr8(c[e] + C_SIDE, 0);
                wr32(c[e] + C_ACTIVE, 0);
            }
            wr32(c[e] + C_ATTACHED, want);
        }
        if (want && k >= 0 && k < RT_MAX_PADS && !o->pads[k]) {
            o->pads[k] = c[e];
            o->plugged++;
        } else o->unplugged[o->nu++] = c[e];
    }
}

/* Entries 1..4 are the plugged-in devices: the keyboard first, after 4 plugged pads if there are 4. */
static void order(uint32_t poll, uint32_t look, uint32_t *c, int n, const sorted_t *s) {
    uint32_t o[MAX_ENTRIES];
    int no = 0;
    if (s->nf + (s->kb != 0) + s->plugged + s->nu != n) return;
    for (int i = 0; i < s->nf; i++) o[no++] = s->first[i];
    if (s->kb && s->plugged < SIDE_SLOTS) o[no++] = s->kb;
    for (int k = 0; k < RT_MAX_PADS; k++)
        if (s->pads[k]) o[no++] = s->pads[k];
    if (s->kb && s->plugged >= SIDE_SLOTS) o[no++] = s->kb;
    for (int i = 0; i < s->nu; i++) o[no++] = s->unplugged[i];
    if (!memcmp(o, c, (size_t)n * sizeof c[0])) return;
    for (int e = 0; e < n; e++) {
        wr32(poll + 4u * (uint32_t)e, o[e]);
        wr32(look + 4u * (uint32_t)e, o[e]);
        wr32(o[e] + C_INDEX, (uint32_t)e);
        wr32(o[e] + C_INDEX2, (uint32_t)e);
        c[e] = o[e];
    }
}

void rt_pads_sync_game(uint32_t frame) {
    uint32_t g = rd32(GAME_PTR), m = g ? rd32(g + G_CTRLMGR) : 0;
    uint32_t poll = m ? rd32(m + M_POLL) : 0, look = m ? rd32(m + M_LOOKUP) : 0;
    if (!poll || !look) return;
    uint32_t c[MAX_ENTRIES];
    int n = 0;
    while (n < MAX_ENTRIES && (c[n] = rd32(look + 4u * (uint32_t)n)) != 0) n++;
    sorted_t s;
    plug(c, n, &s);
    static int fixed = -1;   /* RT_PADS=N (reference runs): the boot order stays */
    if (fixed < 0) fixed = getenv("RT_PADS") != NULL;
    if (!fixed) order(poll, look, c, n, &s);
    mirror_sides(c, n);
    trace(frame, c, n);
}
