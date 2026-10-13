/* party_leave.c -- leaving a party (pt.pause_leave). Closing the page (or LEAVE PARTY) leaves: the relay
 * tells the others ("members"). On the host a friend's controller is unplugged (pad_attach.c: off its
 * side, inactive) and, in a match, the teams are rebuilt as the game rebuilds them at half time
 * (0x6323D0: 0x6AC5E0, FUN_0062CFC0 home and away, 0x6AC780), so the AI takes the leaver's player and
 * the game's disconnect watcher has nothing to stop the match for. The host leaving ends the party: the
 * relay closes the room ("ended") and each friend leaves its match, if any, and is back in its own
 * menus. A friend quitting the host's match from its pause menu leaves the party too. Everyone sees who
 * left (party_toast). Trace: "ms: party left player N", "ms: party ended". */
#include "rt.h"
#include "plat.h"
#include "ui.h"
#include "fe_party.h"
#include "match_state.h"
#include <stdio.h>
#include <string.h>

#define GAME_PTR   0x00A7ED28u
#define VT_BACKEND 0x008CC6F0u
#define FE_PTR     0x013834E8u   /* the front end */
#define FE_RETURN  0x3144Cu      /* front end: the screen a match ends on (net_ha_flow.c) */

static char g_toast[64];
static uint32_t g_toast_until, g_rebuild_at;

void party_toast(uint32_t frame, const char *s) {
    snprintf(g_toast, sizeof g_toast, "%s", s);
    g_toast_until = frame + 240;   /* ~4 s */
}
const char *party_toast_text(uint32_t frame) { return frame < g_toast_until ? g_toast : NULL; }

static int in_match(void) {
    uint32_t g = rd32(GAME_PTR), st = g ? rd32(g + 0x90) : 0;
    return st && rd32(st) == VT_BACKEND;
}

void party_rebuild_teams(void) {   /* the controllers' sides as they are now: the AI takes the rest */
    uint32_t home = ms_team_va(0), away = ms_team_va(1);
    if (!home || !away || !in_match()) return;
    guest_call(0x006AC5E0u, 0);
    guest_call(0x0062CFC0u, 1, home);
    guest_call(0x0062CFC0u, 1, away);
    guest_call(0x006AC780u, 0);
}

/* Every Present, after the controllers follow the party (pads.c). */
void party_leave_present(uint32_t frame) {
    static uint32_t was = 0;
    static int ended_seen;
    uint32_t now = ui_lobby_state() == LOBBY_IN ? ui_lobby_member_mask() : 0;
    char line[64];
    if (g_rebuild_at && frame >= g_rebuild_at) {
        g_rebuild_at = 0;
        if (in_match()) party_rebuild_teams();
    }
    if (rt_net_ha == 1) {
        uint32_t gone = was & ~now & ~1u;
        for (int k = 1; k < 4; k++)
            if (gone >> k & 1) {
                rt_trace("ms: party left player %d%s", k + 1, in_match() ? " (in a match: the AI takes over)" : "");
                snprintf(line, sizeof line, "PLAYER %d LEFT", k + 1);
                party_toast(frame, line);
            }
        if (gone && in_match()) g_rebuild_at = frame + 2;   /* after the controllers are unplugged (pads.c) */
    } else if (rt_net_ha == 2) {
        uint32_t gone = was & ~now;
        for (int k = 1; k < 4; k++)
            if ((gone >> k & 1) && k != ui_lobby_slot()) {
                snprintf(line, sizeof line, "PLAYER %d LEFT", k + 1);
                party_toast(frame, line);
            }
    }
    if (ui_lobby_ended() && !ended_seen) {   /* the host left: the party is over */
        ended_seen = 1;
        rt_trace("ms: party ended (the host left)");
        party_toast(frame, "THE HOST LEFT THE PARTY");
        int playing = in_match();
        ui_lobby_leave();
        uint32_t fe = rd32(FE_PTR);
        if (playing && fe) wr32(fe + FE_RETURN, FE_SCR_MAIN);   /* (the match was to end in the lobby: none now) */
        if (playing) guest_call(0x00576140u, 0);   /* out of the match, as the pause menu's quit does */
        else if (fe_in_menus()) fe_go(FE_SCR_MAIN);
        now = 0;
    }
    if (!ui_lobby_ended()) ended_seen = 0;
    was = now;
}

/* A friend whose game left the host's match by itself (its pause menu's QUIT MATCH) leaves the party. */
void party_leave_quit(uint32_t frame) {
    rt_trace("ms: party friend quit the match: leaving the party");
    party_toast(frame, "YOU LEFT THE PARTY");
    ui_lobby_leave();
}
