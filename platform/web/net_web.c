/* net_web.c -- netplay transport (plat.h net_tx_*) in the browser: the WebSocket (Module.rugbyNet) to the
 * page's server, and for a party's game messages the peer-to-peer data channels (js/rtc.js) when they
 * are open; the game sends through them and drains one message queue. The
 * game loop runs on the page's main thread under JSPI, so waiting is emscripten_sleep (the browser
 * delivers WebSocket messages meanwhile). */
#include "plat.h"
#include "rt.h"
#include <emscripten.h>

EM_JS(int, js_net_send, (const void *p, int n, int text), {
    var net = Module.rugbyNet;
    if (!net || net.closed) return -1;
    var bytes = HEAPU8.slice(p, p + n);
    if (!text && Module.rtc) Module.rtc.mark('tx', bytes);
    if (!text && Module.rtc && Module.rtc.send(bytes)) return 0;   /* peer to peer (js/rtc.js) */
    if (!text && Module.rtc) Module.rtc.stats.ws++;
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
        if (typeof ev.data === 'string' && Module.rtc) {   /* lobby JSON: the peer layer reads it first */
            try {
                if (Module.rtc.text(JSON.parse(ev.data))) return;
            } catch (e) {}
        }
        var u8 = typeof ev.data === 'string' ? new TextEncoder().encode(ev.data) : new Uint8Array(ev.data);
        if (typeof ev.data !== 'string' && Module.rtc) Module.rtc.mark('rx', u8);
        net.queue.push(u8);
    };
    Module.rugbyNet = net;
});
EM_JS(int, js_net_ready, (void), {
    var net = Module.rugbyNet;
    return !net || net.closed ? -1 : net.open ? 1 : 0;
});
EM_JS(void, js_net_close, (void), {
    if (Module.rtc) Module.rtc.leave();
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
// clang-format off
EM_JS(int, js_net_hint, (int which), {   /* the server's hints for a game catching up (the page keeps them) */
    var v = which ? Module.rugbyFront : Module.rugbyGrant;
    return v === undefined ? -1 : v | 0;
});
// clang-format on
int net_tx_hints(int64_t *grant, int64_t *front) {
    *grant = js_net_hint(0);
    *front = js_net_hint(1);
    return *grant >= 0 || *front >= 0;
}

/* The lockstep session's room (the page's, platform/web/shell.html): its code, joining another room (the
   page reloads into it and catches up), leaving for a fresh own game. */
// clang-format off
EM_JS(void, js_session_code, (char *out, int cap), { stringToUTF8(Module.rugbyRoom || '', out, cap); });
EM_JS(void, js_session_join, (const char *code), { if (Module.rugbyJoin) Module.rugbyJoin(UTF8ToString(code)); });
EM_JS(void, js_session_leave, (void), { if (Module.rugbyLeave) Module.rugbyLeave(); });
EM_JS(int, js_page_message, (char *out, int cap), {   /* a message for the game to show, once */
    var m = Module.rugbyMessage || '';
    Module.rugbyMessage = '';
    if (!m) return 0;
    stringToUTF8(m, out, cap);
    return 1;
});
// clang-format on
const char *plat_session_code(void) {
    static char c[16];
    js_session_code(c, sizeof c);
    return c;
}
void plat_session_join(const char *code) { js_session_join(code); }
void plat_session_leave(void) { js_session_leave(); }
const char *plat_page_message(void) {
    static char m[96];
    return js_page_message(m, sizeof m) ? m : NULL;
}
