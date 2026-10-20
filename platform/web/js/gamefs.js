// gamefs.js (emcc --pre-js) -- the game's data in the browser: 1.7 GB of "Game Files" appear under
// "/Game Files" as lazy files backed by the 8 MiB chunks in data/ (tools/web/package_assets.py). The
// runtime reads them with plain POSIX calls, which are synchronous and run on the main thread, where
// fetch/XHR cannot block. So a dedicated worker does the fetching into a SharedArrayBuffer chunk cache
// and a reader waits (spins, on the main thread) until its chunk is resident. Local only: the chunks
// are served by tools/web/serve.py from build/web/data, nothing leaves the machine.
(function () {
  const NSLOT = 32; // resident chunks (NSLOT * chunk_size of shared memory)
  const RING = 64; // pending fetch requests
  // ctl (Int32): 0 lock, 1 requests pushed, 2 use tick, 3 worker fatal error
  // ring at R0: [slot, chunk] pairs; slot table at S0: [state, chunk, lastUse]
  // (state 0 free, 1 loading, 2 ready, 3 failed)
  const R0 = 16;
  const S0 = R0 + RING * 2;
  let ctl, data, chunkBytes, names, sizes;

  function lock() {
    while (Atomics.compareExchange(ctl, 0, 0, 1) !== 0) {
      // spin: the holder only copies a chunk
    }
  }
  function unlock() {
    Atomics.store(ctl, 0, 0);
  }
  function pause(ms) {
    // workers may sleep; the main thread cannot, so it spins
    if (typeof WorkerGlobalScope !== 'undefined') Atomics.wait(ctl, 3, 12345, ms);
  }

  // The slot holding chunk c (hit), else a free one, else the least recently used finished one.
  function findSlot(c) {
    let free = -1;
    let victim = -1;
    let oldest = 0x7fffffff;
    for (let i = 0; i < NSLOT; i++) {
      const b = S0 + i * 3;
      const st = ctl[b];
      if (st !== 0 && ctl[b + 1] === c) return { hit: i };
      if (st === 0) {
        if (free < 0) free = i;
      } else if ((st === 2 || st === 3) && ctl[b + 2] < oldest) {
        oldest = ctl[b + 2];
        victim = i;
      }
    }
    return { hit: -1, slot: free >= 0 ? free : victim };
  }

  // Ask the worker to load chunk c into slot s (the lock is held).
  function request(s, c) {
    const sb = S0 + s * 3;
    const r = Atomics.load(ctl, 1);
    ctl[sb] = 1;
    ctl[sb + 1] = c;
    ctl[sb + 2] = Atomics.add(ctl, 2, 1);
    ctl[R0 + (r % RING) * 2] = s;
    ctl[R0 + (r % RING) * 2 + 1] = c;
    Atomics.add(ctl, 1, 1);
    Atomics.notify(ctl, 1);
  }

  // copy bytes [from, from+n) of chunk `c` (a global chunk number) into dst[doff..]
  function readChunk(c, dst, doff, from, n) {
    for (;;) {
      lock();
      const f = findSlot(c);
      if (f.hit >= 0) {
        const hb = S0 + f.hit * 3;
        if (ctl[hb] === 2) {
          ctl[hb + 2] = Atomics.add(ctl, 2, 1);
          dst.set(data.subarray(f.hit * chunkBytes + from, f.hit * chunkBytes + from + n), doff);
          unlock();
          return;
        }
        if (ctl[hb] === 3) {
          ctl[hb] = 0;
          unlock();
          throw new Error('gamefs: chunk ' + names[c] + ' failed to load');
        }
      } else if (f.slot >= 0) {
        request(f.slot, c);
        unlock();
        continue;
      }
      unlock();
      pause(2); // still loading, or every slot is loading
    }
  }

  // Runs in its own worker (passed as source text): fetch each requested chunk into its slot.
  function workerMain() {
    onmessage = function (e) {
      const m = e.data;
      postMessage('ready'); // running: the game may start reading (startWorker)
      const ctl = new Int32Array(m.ctl);
      const data = new Uint8Array(m.data);
      let done = 0;
      (async function () {
        for (;;) {
          while (Atomics.load(ctl, 1) === done) Atomics.wait(ctl, 1, done);
          const slot = ctl[m.R0 + (done % m.RING) * 2];
          const chunk = ctl[m.R0 + (done % m.RING) * 2 + 1];
          done++;
          let ok = 0;
          try {
            const r = await fetch(m.base + m.names[chunk]);
            if (r.ok) {
              data.set(new Uint8Array(await r.arrayBuffer()), slot * m.chunkBytes);
              ok = 1;
            }
          } catch {
            // a network error fails the chunk (state 3): the reader throws
          }
          Atomics.store(ctl, m.S0 + slot * 3, ok ? 2 : 3);
        }
      })();
    };
  }

  // The game starts only once the worker runs: a read spins the main thread until its chunk is there,
  // and a worker still starting needs that thread to load its script. A read first deadlocked the page
  // (headless Chrome 155, several pages at once: 2 boots in 3 hung at the first lazy read, 2026-10-20).
  function startWorker() {
    ctl = new Int32Array(new SharedArrayBuffer((S0 + NSLOT * 3 + 16) * 4));
    const dataSab = new SharedArrayBuffer(NSLOT * chunkBytes);
    data = new Uint8Array(dataSab);
    const src = '(' + workerMain.toString() + ')()';
    const w = new Worker(URL.createObjectURL(new Blob([src], { type: 'text/javascript' })));
    w.postMessage({
      ctl: ctl.buffer,
      data: dataSab,
      names: names,
      chunkBytes: chunkBytes,
      R0: R0,
      S0: S0,
      RING: RING,
      base: new URL('data/', location.href).href,
    });
    return new Promise((ready) => (w.onmessage = ready));
  }

  function mkdirs(path) {
    try {
      FS.mkdirTree(path);
    } catch {
      // already there
    }
  }

  // One manifest file as a lazy FS node whose reads come from its chunks (c0: its first chunk).
  function mountFile(f, c0) {
    const p = '/Game Files/' + f.path;
    const d = p.slice(0, p.lastIndexOf('/'));
    mkdirs(d);
    const size = f.size;
    const lazy = {
      length: size,
      get: function () {
        throw new Error('gamefs: byte access');
      },
    };
    const node = FS.createFile(d, p.slice(p.lastIndexOf('/') + 1), { isDevice: false, contents: lazy }, true, false);
    node.contents = lazy;
    Object.defineProperties(node, { usedBytes: { get: () => size } });
    const ops = Object.assign({}, node.stream_ops);
    ops.read = function (stream, buffer, offset, length, position) {
      if (position >= size) return 0;
      const n = Math.min(size - position, length);
      let done = 0;
      while (done < n) {
        const pos = position + done;
        const c = Math.floor(pos / chunkBytes);
        const within = pos - c * chunkBytes;
        const take = Math.min(n - done, sizes[c0 + c] - within);
        readChunk(c0 + c, buffer, offset + done, within, take);
        done += take;
      }
      return n;
    };
    node.stream_ops = ops;
  }

  Module.preRun = Module.preRun || [];
  // ?env=RT_A=1,RT_B=2 (diagnosis): the runtime's environment switches, as natively (RT_DET_PAGES...)
  Module.preRun.push(function () {
    const e = new URLSearchParams(location.search).get('env');
    if (e)
      e.split(',').forEach(function (kv) {
        const i = kv.indexOf('=');
        if (i > 0) ENV[kv.slice(0, i)] = kv.slice(i + 1);
      });
  });
  Module.preRun.push(function () {
    addRunDependency('gamefs');
    fetch('data/manifest.json')
      .then((r) => r.json())
      .then(function (man) {
        chunkBytes = man.chunk_size;
        names = [];
        sizes = [];
        const first = man.files.map(function (f) {
          const c0 = names.length;
          f.chunks.forEach(function (c) {
            names.push(c.name);
            sizes.push(c.size);
          });
          return c0;
        });
        man.files.forEach((f, fi) => mountFile(f, first[fi]));
        return startWorker().then(() => removeRunDependency('gamefs'));
      });
  });
})();
