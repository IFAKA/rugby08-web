/* net_web.c -- netplay transport (plat.h net_tx_*) in the browser: the lobby (platform/web/shell.html)
 * owns the WebSocket (Module.rugbyNet); the game sends through it and drains its message queue. The
 * game loop runs on the page's main thread under JSPI, so waiting is emscripten_sleep (the browser
 * delivers WebSocket messages meanwhile). */
#include "plat.h"
#include "rt.h"
#include <emscripten.h>

EM_JS(int, js_net_send, (const void *p, int n, int text), {
    var net = Module.rugbyNet;
    if (!net || net.closed) return -1;
    var bytes = HEAPU8.slice(p, p + n);
    net.ws.send(text ? new TextDecoder().decode(bytes) : bytes);
    return 0;
});
EM_JS(int, js_net_recv, (void *p, int cap), {
    var net = Module.rugbyNet;
    if (!net) return -1;
    if (!net.queue.length) return net.closed ? -1 : 0;
    var m = net.queue.shift(), n = Math.min(m.length, cap);
    HEAPU8.set(m.subarray(0, n), p);
    return n;
});
EM_JS(void, js_net_status, (int kind, int value), {
    if (Module.rugbyNetStatus) Module.rugbyNetStatus(kind, value);
});
EM_JS(int, js_net_delay, (void), { return Module.rugbyNetDelay ? Module.rugbyNetDelay() | 0 : 0; });

EM_JS(int, Module_has_net, (void), { return Module.rugbyNet ? 1 : 0; });
/* The in-game Online screens (runtime/core/ui_lobby.c) open their own socket to the page's server; its
   lobby replies (text) are queued as bytes too. 1 open, -1 closed, 0 still connecting. */
// clang-format off
EM_JS(void, js_net_connect, (void), {
    var ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
    var net = { ws: ws, queue: [], closed: false, open: false };
    ws.binaryType = 'arraybuffer';
    ws.onopen = () => { net.open = true; };
    ws.onclose = () => { net.closed = true; };
    ws.onmessage = (ev) => {
        net.queue.push(typeof ev.data === 'string' ? new TextEncoder().encode(ev.data) : new Uint8Array(ev.data));
    };
    Module.rugbyNet = net;
});
EM_JS(int, js_net_ready, (void), {
    var net = Module.rugbyNet;
    return !net || net.closed ? -1 : net.open ? 1 : 0;
});
EM_JS(void, js_net_close, (void), {
    if (Module.rugbyNet && Module.rugbyNet.ws) Module.rugbyNet.ws.close();
    Module.rugbyNet = null;
});
// clang-format on
int net_tx_open(const char *url) {
    (void)url;   /* the page's own server */
    if (Module_has_net()) return 0;
    js_net_connect();
    for (int waited = 0; waited < 5000; waited += 20) {
        int r = js_net_ready();
        if (r) return r > 0 ? 0 : -1;
        emscripten_sleep(20);
    }
    return -1;
}
void net_tx_close(void) { js_net_close(); }
int net_tx_send(const void *data, int len, int text) { return js_net_send(data, len, text); }
int net_tx_recv(void *buf, int cap, int wait_ms) {
    int n = js_net_recv(buf, cap);
    if (n == 0 && wait_ms > 0) {
        emscripten_sleep((unsigned)wait_ms);
        n = js_net_recv(buf, cap);
    }
    return n;
}
void net_tx_status(int kind, int value) { js_net_status(kind, value); }
int net_tx_delay(void) { return js_net_delay(); }
