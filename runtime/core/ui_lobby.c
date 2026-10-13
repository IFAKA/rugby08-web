/* ui_lobby.c -- the Online screens' rooms: connect to the relay (server/), host a room (the server makes
 * the code) or join one by its code, then play host-authoritative (net_ha.c: the room's creator hosts).
 * Lobby messages are JSON text; the session's are binary (net_ha.c hands text on to ui_lobby_text). */
#include "rt.h"
#include "plat.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *rt_net_url = "ws://127.0.0.1:8080/ws";   /* --net URL (net_join.c) */
static int g_state, g_connected, g_members, g_host;
static uint32_t g_mask;   /* bit k: room slot k has a member */
static char g_code[16], g_err[96];

static int field(const char *json, int n, const char *key, char *out, int cap) {   /* "key":"value" or :number */
    char pat[32];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = json, *end = json + n;
    for (; p + strlen(pat) <= end && strncmp(p, pat, strlen(pat)) != 0; p++) {}
    if (p + strlen(pat) > end) return 0;
    p += strlen(pat);
    int quoted = *p == '"', k = 0;
    p += quoted;
    while (p < end && k < cap - 1 && (quoted ? *p != '"' : (*p != ',' && *p != '}'))) out[k++] = *p++;
    out[k] = 0;
    return 1;
}

static int send_json(const char *t, const char *room) {
    if (!g_connected) {
        if (net_tx_open(rt_net_url)) {
            g_state = LOBBY_ERROR;
            snprintf(g_err, sizeof g_err, "CANNOT REACH THE SERVER");
            return 0;
        }
        g_connected = 1;
    }
    char m[160];
    snprintf(m, sizeof m, "{\"t\":\"%s\",\"room\":\"%s\",\"name\":\"%s\"}", t, room,
             rt_net_ha == 1 ? "host" : "player");
    net_tx_send(m, (int)strlen(m), 1);
    g_state = LOBBY_WAITING;
    rt_trace("ms: ui lobby %s %s", t, room);
    return 1;
}

void ui_lobby_host(void) { send_json("host", ""); }
void ui_lobby_join(const char *code) { send_json("join", code); }

void ui_lobby_text(const char *json, int n) {
    char v[96];
    if (field(json, n, "t", v, sizeof v) && !strcmp(v, "joined")) {
        char slot[8], creator[8];
        field(json, n, "room", g_code, sizeof g_code);
        field(json, n, "slot", slot, sizeof slot);
        field(json, n, "creator", creator, sizeof creator);
        g_host = !strcmp(creator, "true");
        g_state = LOBBY_IN;
        g_members = 1;
        g_mask = 1u << atoi(slot);
        rt_trace("ms: ui room %s as player %d (%s)", g_code, atoi(slot) + 1, g_host ? "host" : "friend");
        rt_net_begin_ha(atoi(slot), g_host);
    } else if (field(json, n, "t", v, sizeof v) && !strcmp(v, "error")) {
        field(json, n, "msg", g_err, sizeof g_err);
        g_state = LOBBY_ERROR;
        rt_trace("ms: ui lobby error %s", g_err);
    } else if (field(json, n, "t", v, sizeof v) && !strcmp(v, "members")) {
        int c = 0;   /* one "slot" per member */
        uint32_t mask = 0;
        for (const char *p = json; p + 8 < json + n; p++)
            if (!strncmp(p, "\"slot\":", 7)) {
                c++;
                int k = atoi(p + 7);
                if (k >= 0 && k < 32) mask |= 1u << k;
            }
        if (mask != g_mask) rt_trace("ms: ui room %s member slots %X", g_code, mask);
        g_mask = mask;
        if (c != g_members) rt_trace("ms: ui room %s members %d", g_code, c);
        g_members = c;
    }
}

/* Before the session starts, lobby replies are read here (afterwards net_ha.c reads everything). */
void ui_lobby_poll(void) {
    if (!g_connected || rt_net_ha) return;
    char buf[2048];
    int n;
    while ((n = net_tx_recv(buf, sizeof buf - 1, 0)) > 0) {
        buf[n] = 0;
        if (buf[0] == '{') ui_lobby_text(buf, n);
        if (rt_net_ha) break;
    }
}

void ui_lobby_leave(void) {
    rt_trace("ms: ui leaves room %s", g_code);
    net_tx_close();
    g_connected = 0;
    g_state = LOBBY_IDLE;
    g_code[0] = 0;
    g_mask = 0;
    rt_net_end_ha();
}

int ui_lobby_state(void) { return g_state; }
const char *ui_lobby_code(void) { return g_code; }
const char *ui_lobby_error(void) { return g_err; }
int ui_lobby_members(void) { return g_members; }
uint32_t ui_lobby_member_mask(void) { return g_mask; }
int ui_lobby_is_host(void) { return g_host; }
