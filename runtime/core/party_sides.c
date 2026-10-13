/* party_sides.c -- Team Select in a party (pt.sides): on every party member's own screen, in sync.
 * The host's Team Select (screen 6) decides: its friends' pads are its controllers (pad_attach.c), each
 * player moves its own icon; a side's team, category and kit are changed by that side's captain only
 * (the host if on it, else the lowest player number on it), CONTINUE and BACK by the host only (the
 * game's callbacks, game/overrides/menus/team_select.c, ask party_ts_allow). The host sends its state
 * ('T': each player's side, both sides' category, team and kit as the toggles show them) and its
 * friends' games show it: they follow it into Team Select (and back to their lobby), their controllers
 * stand for the party's (pad_attach.c mirror) and their toggles are stepped with the game's own
 * callbacks until they show the host's texts (texts, not indices: the lists may differ by profile).
 * Trace: "ms: sides TEXT" (the host's state when it changes, a friend's when it matches). */
#include "rt.h"
#include "plat.h"
#include "ui.h"
#include "fe_party.h"
#include "match_state.h"
#include "pad_attach.h"
#include <stdio.h>
#include <string.h>

#define FE_PTR    0x013834E8u
#define FE_SIDE   0x31478u   /* FE: the side of the controller whose message is handled (1 home, 2 away) */
#define GAME_PTR  0x00A7ED28u
#define TS_SCREEN 6
#define OPT_LEN   32
enum { CATEGORY, TEAM, KIT, NTOGGLE };
static const int k_win[2][NTOGGLE] = { { 9, 9, 10 }, { 19, 19, 20 } }, k_id[NTOGGLE] = { 28, 15, 27 };
/* the game's callbacks that step each toggle (the side from FE_SIDE): category R1, team down, kit right */
static const uint32_t k_step[NTOGGLE] = { 0x004FAE90u, 0x004FA960u, 0x004FB970u };

typedef struct {
    char side[4];                       /* party player 1..4: 'H', 'A' or 0 */
    char opt[2][NTOGGLE][OPT_LEN];      /* home/away: category, team, kit */
} ts_state_t;

static ts_state_t g_host;   /* friend: the host's, from 'T' */
static int g_have, g_stepping;

static uint32_t find(uint32_t head, int id) {   /* FE list {+4 id, +8 object, +0x10 next}, circular */
    for (uint32_t n = head; n; n = rd32(n + 0x10)) {
        uint32_t o = rd32(n + 8);
        if (o && rd16(o + 4) == id) return o;
        if (rd32(n + 0x10) == head) break;
    }
    return 0;
}

static const char *option(uint32_t screen, int side, int t) {   /* a toggle's current option text */
    uint32_t w = find(screen + 0x10, k_win[side][t]), tg = w ? find(w + 0xA0, k_id[t]) : 0;
    uint32_t cur = tg ? rd32(tg + 0x158) : 0;
    const char *s = cur ? gstr(rd32(cur)) : NULL;
    return s ? s : "";
}

static void state(uint32_t screen, ts_state_t *st) {
    memset(st, 0, sizeof *st);
    st->side[0] = rt_pads_side(-1) ? rt_pads_side(-1) : rt_pads_side(0);   /* the host: keyboard or controller */
    for (int k = 1; k < 4; k++) st->side[k] = rt_pads_side(k);
    for (int s = 0; s < 2; s++)
        for (int t = 0; t < NTOGGLE; t++) snprintf(st->opt[s][t], OPT_LEN, "%s", option(screen, s, t));
}

static void trace(const ts_state_t *st, const char *who) {
    rt_trace("ms: sides %s %c%c%c%c home %s / %s / %s away %s / %s / %s", who, st->side[0] ? st->side[0] : '-',
             st->side[1] ? st->side[1] : '-', st->side[2] ? st->side[2] : '-', st->side[3] ? st->side[3] : '-',
             st->opt[0][0], st->opt[0][1], st->opt[0][2], st->opt[1][0], st->opt[1][1], st->opt[1][2]);
}

void party_sides_rx(const uint8_t *p, size_t len) {   /* friend: 'T' */
    if (len != sizeof g_host) return;
    memcpy(&g_host, p, sizeof g_host);
    for (int s = 0; s < 2; s++)
        for (int t = 0; t < NTOGGLE; t++) g_host.opt[s][t][OPT_LEN - 1] = 0;
    g_have = 1;
}

/* host: may this message's controller do it? (team, category, kit: its side's captain; others: the host) */
int party_ts_allow(int what) {
    if (rt_net_ha == 2) return g_stepping;   /* a friend's Team Select only shows the host's */
    if (rt_net_ha != 1 || fe_screen_id() != TS_SCREEN) return 1;
    uint32_t g = rd32(GAME_PTR), m = g ? rd32(g + 0x10) : 0, fe = rd32(FE_PTR), sv = g_ecx;
    if (!m) return 1;
    g_ecx = m;
    uint32_t c = guest_call(0x0081A5E0u, 1, 4u);   /* the controller with input (the sender) */
    g_ecx = sv;
    int player = 0;   /* the host's keyboard, mouse or its own controller */
    if (c && rd32(c + 0x2570) == 5) {
        int k = rt_dinput_pad_slot(rd32(c + 0x25E4));
        player = k > 0 ? k : 0;
    }
    if (what == PARTY_TS_HOST) return player == 0;
    char side = rd8(fe + FE_SIDE) == 1 ? 'H' : rd8(fe + FE_SIDE) == 2 ? 'A' : 0;
    ts_state_t st;
    state(rd32(fe + 4), &st);
    int captain = -1;
    for (int k = 0; k < 4 && captain < 0; k++)
        if (side && st.side[k] == side) captain = k;
    int ok = !side || captain < 0 || captain == player;
    if (!ok) rt_trace("ms: sides player %d is not side %c's captain (%d)", player + 1, side, captain + 1);
    return ok;
}

static void host_step(uint32_t frame, uint32_t screen) {
    static ts_state_t last;
    static uint32_t sent;
    ts_state_t st;
    state(screen, &st);
    if (memcmp(&st, &last, sizeof st) == 0 && frame - sent < 60) return;
    if (memcmp(&st, &last, sizeof st) != 0) trace(&st, "host");
    last = st;
    sent = frame;
    net_ha_send('T', frame, &st, sizeof st);
}

/* This player moved its icon on its own screen (it shows the host's Team Select): stick pulses toward that
   side until the host's game has it there (each pulse is one step, home - centre - away), 1.5 s at most. */
static void own_move(uint32_t frame) {
    static char want;
    static uint32_t until, last;
    char i = rt_pads_take_intent();
    int me = ui_lobby_slot();
    if (i) want = i, until = frame + 90, rt_trace("ms: sides friend moved its icon to %c", i);
    if (!want || me < 1 || me > 3) return;
    char at = g_host.side[me] ? g_host.side[me] : 'C';
    int pos_at = at == 'H' ? -1 : at == 'A' ? 1 : 0, pos_want = want == 'H' ? -1 : want == 'A' ? 1 : 0;
    if (pos_at == pos_want || frame > until) {
        want = 0;
        return;
    }
    if (frame - last >= 12) {
        net_ha_nudge(pos_want < pos_at ? -1 : 1, 5);
        last = frame;
    }
}

static void friend_step(uint32_t frame, uint32_t screen) {
    char sides[RT_MAX_PADS + 1] = { 0 };
    for (int k = 1; k < 4; k++) sides[k] = g_host.side[k];
    sides[RT_MAX_PADS] = g_host.side[0];   /* the host's icon: this game's keyboard entry */
    rt_pads_mirror_own(ui_lobby_slot());
    rt_pads_mirror(1, sides);
    own_move(frame);
    uint32_t fe = rd32(FE_PTR);
    uint8_t keep = rd8(fe + FE_SIDE);
    int changed = 0;
    for (int s = 0; s < 2; s++)
        for (int t = 0; t < NTOGGLE; t++)   /* category first: it decides the team list */
            for (int n = 0; n < 64 && strcmp(option(screen, s, t), g_host.opt[s][t]) != 0; n++) {
                const char *was = option(screen, s, t);
                char before[OPT_LEN];
                snprintf(before, sizeof before, "%s", was);
                wr8(fe + FE_SIDE, (uint8_t)(s + 1));
                g_stepping = 1;
                guest_call(k_step[t], 1, 0u);
                g_stepping = 0;
                changed = 1;
                if (!strcmp(option(screen, s, t), before)) break;   /* it does not move: not in this list */
            }
    wr8(fe + FE_SIDE, keep);
    if (changed) {
        ts_state_t st;
        state(screen, &st);
        memcpy(st.side, g_host.side, sizeof st.side);
        trace(&st, "friend");
    }
}

/* every Present (fe_party.c) */
void party_sides_present(uint32_t frame) {
    if (ui_lobby_state() != LOBBY_IN || !fe_in_menus()) {
        rt_pads_mirror(0, NULL);
        return;
    }
    uint32_t fe = rd32(FE_PTR), screen = fe ? rd32(fe + 4) : 0;
    int id = fe_screen_id(), host_at = net_ha_host_screen();
    if (rt_net_ha == 1) {
        if (id == TS_SCREEN) host_step(frame, screen);
        return;
    }
    static uint32_t asked;
    if (host_at == TS_SCREEN && id != TS_SCREEN && (id == FE_SCR_PARTY || id == FE_SCR_MAIN) && asked != screen) {
        asked = screen;   /* into the host's Team Select (once per screen we are on) */
        fe_go(TS_SCREEN);
    } else if (id == TS_SCREEN && host_at != TS_SCREEN && host_at >= 0 && asked != screen) {
        asked = screen;   /* the host left it: back to the lobby */
        rt_pads_mirror(0, NULL);
        fe_go(FE_SCR_PARTY);
    }
    if (id == TS_SCREEN && g_have) friend_step(frame, screen);
    else rt_pads_mirror(0, NULL);
}
