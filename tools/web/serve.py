#!/usr/bin/env python3
"""Serve a directory on localhost only, with COOP/COEP so SharedArrayBuffer (pthreads) works.
usage: serve.py [DIR] [PORT]"""

import functools
import http.server
import os
import sys


class H(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "text/javascript",
        ".mjs": "text/javascript",
    }

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *a):
        with open("logs/serve.log", "a") as f:
            f.write(fmt % a + "\n")


if __name__ == "__main__":
    d = sys.argv[1] if len(sys.argv) > 1 else "build/web"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
    os.makedirs("logs", exist_ok=True)
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(H, directory=d))
    print(f"serving {d} on http://127.0.0.1:{port}/ (COOP/COEP)", flush=True)
    srv.serve_forever()
