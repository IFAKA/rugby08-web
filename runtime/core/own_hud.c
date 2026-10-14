/* own_hud.c -- a party: what the game draws for one player's action is drawn on that player's team's
 * screens only, as playing alone against the computer (the user, 2026-10-14: a friend's kick showed its
 * aim arrow on the other team's screen; "it should look like the AI is playing"). The game draws every
 * human's prompts (a couch match, one TV); each game leaves the other team's out of its picture at the
 * device (runtime/d3d8: no game memory, no guest code: the games stay alike).
 *   aim arrows  the kick's aim (the arc and its shadow) and the other arrow models made with it: the
 *               game's static meshes, known by their vertex data (fvf 0x52), drawn while the kicker holds
 *               the ball: left out unless this player is on the ball holder's team.
 * Found with runtime/debug/draw_who.c (RT_DRAW_WHO, RT_DRAW_HIDE_VB): the arrow's draws come from the
 * engine's render lists, so the buffer, not the code that queued it, tells the part. */
#include "rt.h"
#include "plat.h"
#include "fe_party.h"
#include "ls_roles.h"
#include "match_state.h"
#include "own_hud.h"
#include <stdlib.h>

static const struct {
    uint32_t len, fvf, hash;
    int part;
} k_parts[] = {
    { 6076, 0x52, 0x738FDA85u, OWN_HUD_AIM },   /* the aim's arc (made at the match's load, measured) */
    { 2212, 0x52, 0xAEDEF60Au, OWN_HUD_AIM },   /* its shadow (two copies, the same mesh) */
};

static uint32_t fnv(const uint8_t *p, uint32_t n) {
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

/* A static vertex buffer's content once written: the HUD part it is, or 0. */
int own_hud_part(const uint8_t *data, uint32_t len, uint32_t fvf) {
    for (size_t i = 0; i < sizeof k_parts / sizeof *k_parts; i++) {
        if (k_parts[i].len != len || k_parts[i].fvf != fvf) continue;
        uint32_t h = fnv(data, len);
        static int said;
        if (getenv("RT_DRAW_WHO") && said++ < 16) rt_trace("own hud: buffer len %u fvf %X hash %08X", len, fvf, h);
        if (k_parts[i].hash == h) return k_parts[i].part;
    }
    return 0;
}

/* The team acting now: the ball's holder's, else the team of the last match event about one of its
   players (a kick-off's kicker stands by the ball on the tee: nobody holds it while the aim is drawn,
   measured). Read at every draw and event, every game alike (no game memory written). */
static int g_acting = -1;
void own_hud_event(uint32_t event) {
    int what, t = event ? ms_team_of(rd32(event + 8), &what) : -1;
    if (t >= 0 && what == 2) g_acting = t;
}
int own_hud_acting(void) {
    ms_ball_t b;
    if (ms_ball(&b) && b.holder_team >= 0) g_acting = (int)(unsigned char)b.holder_team;
    return g_acting;
}

/* Whether this player's picture leaves out a draw of PART. Trace: "ms: own hud part N left out / shown"
   (shown: drawn again, or no longer drawn: own_hud_present). */
static int g_said = -1, g_part;
static uint32_t g_last;   /* the frame of the last draw left out */
int own_hud_hidden(int part) {
    if (!part || !ls_on() || __builtin_popcount(ls_members()) < 2) return 0;
    int me = ls_team_of(rt_net_slot()), t = own_hud_acting(), hide = me >= 0 && t >= 0 && t != me; /* (watching: all) */
    if (hide) g_last = rt_present_frame, g_part = part;
    if (hide != g_said)
        rt_trace("ms: own hud part %d %s (team %d, acting %d) at frame %u", part, hide ? "left out" : "shown", me, t,
                 rt_present_frame);
    g_said = hide;
    return hide;
}

void own_hud_present(void) { /* every Present */
    if (g_said == 1 && rt_present_frame > g_last + 1) {
        rt_trace("ms: own hud part %d shown (no longer drawn) at frame %u", g_part, rt_present_frame);
        g_said = 0;
    }
}
