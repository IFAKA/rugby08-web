/* net_join.c -- native netplay start from the command line: connect to the relay, join the room,
 * then hand over to the lockstep session (netplay.c rt_net_begin). The web page joins by itself. */
#include "rt.h"
#include "plat.h"
#include "ui.h"
#include "own_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --net URL --net-room CODE [--net-name NAME] [--net-delay D] [--net-mode ha]: join the room (the first to join creates
 * it and plays from frame 0; later ones catch up). Returns 1 if a session started. */
int rt_net_cfg_delay;   /* --net-delay D (0: the default) */
const char *rt_net_room;   /* --net-room CODE (the session's room: plat_session_code) */
int rt_net_start(int argc, char **argv) {
    const char *url = NULL, *room = NULL, *name = "player";
    int ha = 0;   /* --net-mode ha: host-authoritative (net_ha.c) instead of lockstep */
    for (int i = 1; i < argc; i++)   /* --own-camera: each player's own camera (own_camera.c) */
        if (!strcmp(argv[i], "--own-camera")) rt_own_camera = 1;
    for (int i = 1; i + 1 < argc; i++) {
        int s, c;   /* --net-session SLOT,CREATOR: the web page already joined the room */
        if (!strcmp(argv[i], "--net-session") && sscanf(argv[i + 1], "%d,%d", &s, &c) == 2) return rt_net_begin(s, c);
        if (!strcmp(argv[i], "--net")) url = argv[++i];
        else if (!strcmp(argv[i], "--net-room")) room = argv[++i];
        else if (!strcmp(argv[i], "--net-name")) name = argv[++i];
        else if (!strcmp(argv[i], "--net-delay")) rt_net_cfg_delay = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--net-mode")) ha = !strcmp(argv[++i], "ha");
    }
    if (url) rt_net_url = url;   /* the relay the Online screens use */
    rt_net_room = room;
    if (!url || !room) return 0;
    if (net_tx_open(url)) rt_fatal("netplay: cannot connect to %s", url);
    char msg[256];
    snprintf(msg, sizeof msg, "{\"t\":\"join\",\"room\":\"%s\",\"name\":\"%s\",\"auto\":true}", room, name);
    net_tx_send(msg, (int)strlen(msg), 1);
    char buf[2048];
    int n;
    while ((n = net_tx_recv(buf, sizeof buf - 1, 1000)) >= 0) {   /* until "joined" */
        if (!n || buf[0] != '{') continue;
        buf[n] = 0;
        const char *p;
        if (strstr(buf, "\"t\":\"error\"")) rt_fatal("netplay: %s", buf);
        if (strstr(buf, "\"t\":\"joined\"") && (p = strstr(buf, "\"slot\":")))
            return (ha ? rt_net_begin_ha : rt_net_begin)(atoi(p + 7), strstr(buf, "\"creator\":true") != NULL);
    }
    rt_fatal("netplay: lobby failed");
    return 0;
}
