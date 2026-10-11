/* net_native.c -- netplay transport (plat.h net_tx_*) for the native build: a minimal WebSocket
 * client over a plain TCP socket (ws:// only; the relay server is server/main.go). */
#include "plat.h"
#include "rt.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int g_fd = -1;
static uint8_t g_rb[1 << 16];
static int g_rn;

static int read_some(int wait_ms) {
    struct pollfd p = { g_fd, POLLIN, 0 };
    if (poll(&p, 1, wait_ms) <= 0) return 0;
    ssize_t n = recv(g_fd, g_rb + g_rn, sizeof g_rb - (size_t)g_rn, 0);
    if (n <= 0) return -1;
    g_rn += (int)n;
    return (int)n;
}

int net_tx_open(const char *url) {   /* ws://host:port/path */
    char host[256] = "127.0.0.1", path[256] = "/ws";
    int port = 80;
    if (sscanf(url, "ws://%255[^:/]:%d%255s", host, &port, path) < 2 && sscanf(url, "ws://%255[^/]%255s", host, path) < 1) return -1;
    struct addrinfo hints = { 0 }, *ai;
    hints.ai_socktype = SOCK_STREAM;
    char ps[16]; snprintf(ps, sizeof ps, "%d", port);
    if (getaddrinfo(host, ps, &hints, &ai)) return -1;
    g_fd = socket(ai->ai_family, SOCK_STREAM, 0);
    if (g_fd < 0 || connect(g_fd, ai->ai_addr, ai->ai_addrlen)) { freeaddrinfo(ai); return -1; }
    freeaddrinfo(ai);
    char req[512];
    int n = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Key: cnVnYnkwOC1uZXRwbGF5IQ==\r\nSec-WebSocket-Version: 13\r\n\r\n", path, host, port);
    if (send(g_fd, req, (size_t)n, 0) != n) return -1;
    for (int tries = 0; tries < 100; tries++) {   /* the 101 response ends with an empty line */
        if (read_some(100) < 0) return -1;
        uint8_t *e = g_rn >= 4 ? memmem(g_rb, (size_t)g_rn, "\r\n\r\n", 4) : NULL;
        if (e) {
            if (memcmp(g_rb, "HTTP/1.1 101", 12)) return -1;
            int used = (int)(e + 4 - g_rb);
            memmove(g_rb, g_rb + used, (size_t)(g_rn - used)); g_rn -= used;
            return 0;
        }
    }
    return -1;
}

int net_tx_send(const void *data, int len, int text) {
    uint8_t h[14];
    int hn = 0;
    h[hn++] = (uint8_t)(0x80 | (text ? 1 : 2));
    if (len < 126) h[hn++] = (uint8_t)(0x80 | len);
    else { h[hn++] = 0x80 | 126; h[hn++] = (uint8_t)(len >> 8); h[hn++] = (uint8_t)len; }
    h[hn++] = 0; h[hn++] = 0; h[hn++] = 0; h[hn++] = 0;   /* zero mask key: payload unchanged */
    if (send(g_fd, h, (size_t)hn, 0) != hn) return -1;
    return send(g_fd, data, (size_t)len, 0) == len ? 0 : -1;
}

/* Next message payload into buf (>0 its length), 0 if none within wait_ms, -1 if closed. */
int net_tx_recv(void *buf, int cap, int wait_ms) {
    for (;;) {
        if (g_rn >= 2) {
            int len = g_rb[1] & 0x7F, hn = 2;
            if (len == 126) { if (g_rn < 4) goto more; len = g_rb[2] << 8 | g_rb[3]; hn = 4; }
            else if (len == 127) return -1;
            if (g_rn >= hn + len) {
                int op = g_rb[0] & 0x0F;
                int n = len < cap ? len : cap;
                memcpy(buf, g_rb + hn, (size_t)n);
                memmove(g_rb, g_rb + hn + len, (size_t)(g_rn - hn - len)); g_rn -= hn + len;
                if (op == 8) return -1;
                if (op == 1 || op == 2) return n;
                continue;   /* ping/pong/continuation: ignored */
            }
        }
    more:
        { int r = read_some(wait_ms); if (r < 0) return -1; if (r == 0) return 0; wait_ms = 0; }
    }
}
