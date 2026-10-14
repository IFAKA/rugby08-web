# platform/web/pwa/ — the installable app

The browser build is a Progressive Web App: `manifest.webmanifest` (name, icons, fullscreen landscape),
`sw.js` (service worker: page and engine network-first, the game's data chunks cache-first, so the game
starts offline once played) and the icons (`icon-*.png`, drawn for this project, not EA artwork).
CMake copies this directory into `build/web/pwa/` and `sw.js` / `manifest.webmanifest` next to
`index.html` (a worker's scope is its directory); `platform/web/shell.html` links the manifest and
registers the worker. Browsers offer "Install" on `localhost` or HTTPS.

Updates without clearing anything: each build stamps its id into `sw.js` (`tools/web/stamp_sw.cmake`), the
page registers it with `updateViaCache: 'none'` and checks for a new one at every start; a new worker takes
over at once (skipWaiting, clients.claim) and deletes the old build's cache. The page and engine are fetched
network-first and revalidated, so an online start always runs the newest build; offline, the last one.
