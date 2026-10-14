# tools/web/ — local web serving and the browser build's helpers

`serve.py` serves build/web on 127.0.0.1 with COOP/COEP (SharedArrayBuffer); `package_assets.py` chunks Game Files
into build/web/data (your own copy: never published). `stamp_sw.cmake` writes the service worker with the build's id
(each build updates an installed app by itself, platform/web/pwa/). `web_selftest.py`, `web_profile.py`: checks
and a profile in headless Chrome.
