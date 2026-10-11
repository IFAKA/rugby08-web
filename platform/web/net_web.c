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
EM_JS(void, js_net_status, (int waiting_frames), {
  if (Module.rugbyNetStatus) Module.rugbyNetStatus(waiting_frames);
});

EM_JS(int, Module_has_net, (void), { return Module.rugbyNet ? 1 : 0; });
int net_tx_open(const char *url) { (void)url; return Module_has_net() ? 0 : -1; }
int net_tx_send(const void *data, int len, int text) { return js_net_send(data, len, text); }
int net_tx_recv(void *buf, int cap, int wait_ms) {
    int n = js_net_recv(buf, cap);
    if (n == 0 && wait_ms > 0) { emscripten_sleep((unsigned)wait_ms); n = js_net_recv(buf, cap); }
    return n;
}
void net_tx_status(int waiting_frames) { js_net_status(waiting_frames); }
