# platform/web/ — the browser build

`platform.c` arena (1 GiB), the per-frame yield to the browser (JSPI) · `shell.html` the page (emcc --shell-file)
· `js/gamefs.js` Game Files as lazy chunks · `js/audio.js` AudioWorklet output · `js/errors.js` page errors · `pwa/` the installable app (manifest, service worker, icons).
Served locally by `tools/serve.py` (COOP/COEP for SharedArrayBuffer). Nothing is deployed.
