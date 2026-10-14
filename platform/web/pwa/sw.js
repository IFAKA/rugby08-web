// sw.js -- the installable app's service worker (platform/web/pwa/README.md). The page and the engine
// (index.html, rugby08.js/.wasm/.data) come from the network when it is there, so a new build is picked
// up at once, and from the cache when it is not; the game's data chunks (data/*: the game files, never
// changed) come from the cache first. Responses keep their headers (COOP/COEP: SharedArrayBuffer).
// Updates: every build stamps its id below (tools/web/stamp_sw.cmake), so a deployed build is a new
// worker; it takes over at once and drops the old build's cache (the data chunks are kept).
const BUILD = '@BUILD@';
const CACHE = 'rugby08-' + BUILD;
const DATA = 'rugby08-data';
const SHELL = ['./', './index.html', './rugby08.js', './rugby08.wasm', './rugby08.data', './manifest.webmanifest'];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(SHELL).catch(() => {})).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches
      .keys()
      .then((ks) => Promise.all(ks.filter((k) => k !== CACHE && k !== DATA).map((k) => caches.delete(k))))
      .then(() => self.clients.claim()),
  );
});

async function cacheFirst(req) {
  const hit = await caches.match(req);
  if (hit) return hit;
  const res = await fetch(req);
  if (res.ok) (await caches.open(DATA)).put(req, res.clone());
  return res;
}

async function networkFirst(req) {
  try {
    const res = await fetch(req, { cache: 'no-cache' });   // (revalidated: never an old build from the HTTP cache)
    if (res.ok) (await caches.open(CACHE)).put(req, res.clone());
    return res;
  } catch (err) {
    const hit = await caches.match(req);
    if (hit) return hit;
    throw err;
  }
}

self.addEventListener('fetch', (e) => {
  const url = new URL(e.request.url);
  if (e.request.method !== 'GET' || url.origin !== self.location.origin || url.pathname.endsWith('/ws')) return;
  e.respondWith(url.pathname.includes('/data/') ? cacheFirst(e.request) : networkFirst(e.request));
});
