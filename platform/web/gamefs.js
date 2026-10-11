// gamefs.js (emcc --pre-js) -- the game's data in the browser: 1.7 GB of "Game Files" appear under
// "/Game Files" as lazy files backed by the 8 MiB chunks in data/ (tools/package_assets.py). The
// runtime reads them with plain POSIX calls, which are synchronous and run on the main thread, where
// fetch/XHR cannot block. So a dedicated worker does the fetching into a SharedArrayBuffer chunk cache
// and a reader waits (spins, on the main thread) until its chunk is resident. Local only: the chunks
// are served by tools/serve.py from build/web/data, nothing leaves the machine.
(function () {
  var NSLOT = 32;                      // resident chunks (NSLOT * chunk_size of shared memory)
  var RING = 64;                       // pending fetch requests
  // ctl (Int32): 0 lock, 1 requests pushed, 2 use tick, 3 worker fatal error
  // ring at R0: [slot, chunk] pairs; slot table at S0: [state, chunk, lastUse] (state 0 free, 1 loading, 2 ready, 3 failed)
  var R0 = 16, S0 = R0 + RING * 2;
  var ctl, data, slots, chunkBytes, names, sizes;

  function lock() { while (Atomics.compareExchange(ctl, 0, 0, 1) !== 0) {} }
  function unlock() { Atomics.store(ctl, 0, 0); }
  function pause(ms) { // workers may sleep; the main thread cannot, so it spins
    if (typeof WorkerGlobalScope !== 'undefined') Atomics.wait(ctl, 3, 12345, ms);
  }

  // copy bytes [from, from+n) of chunk `c` (a global chunk number) into dst[doff..]
  function readChunk(c, dst, doff, from, n) {
    for (;;) {
      lock();
      var hit = -1, free = -1, victim = -1, oldest = 0x7fffffff;
      for (var i = 0; i < NSLOT; i++) {
        var b = S0 + i * 3, st = ctl[b];
        if (st !== 0 && ctl[b + 1] === c) { hit = i; break; }
        if (st === 0) { if (free < 0) free = i; }
        else if (st === 2 || st === 3) { if (ctl[b + 2] < oldest) { oldest = ctl[b + 2]; victim = i; } }
      }
      if (hit >= 0) {
        var hb = S0 + hit * 3;
        if (ctl[hb] === 2) {
          ctl[hb + 2] = Atomics.add(ctl, 2, 1);
          dst.set(data.subarray(hit * chunkBytes + from, hit * chunkBytes + from + n), doff);
          unlock();
          return;
        }
        if (ctl[hb] === 3) { ctl[hb] = 0; unlock(); throw new Error('gamefs: chunk ' + names[c] + ' failed to load'); }
        unlock(); pause(2); continue;     // still loading
      }
      var s = free >= 0 ? free : victim;
      if (s < 0) { unlock(); pause(2); continue; }   // every slot is loading
      var sb = S0 + s * 3, r = Atomics.load(ctl, 1);
      ctl[sb] = 1; ctl[sb + 1] = c; ctl[sb + 2] = Atomics.add(ctl, 2, 1);
      ctl[R0 + (r % RING) * 2] = s; ctl[R0 + (r % RING) * 2 + 1] = c;
      Atomics.add(ctl, 1, 1);
      Atomics.notify(ctl, 1);
      unlock();
    }
  }

  function workerMain() {
    onmessage = function (e) {
      var m = e.data, ctl = new Int32Array(m.ctl), data = new Uint8Array(m.data), done = 0;
      var R0 = m.R0, S0 = m.S0, RING = m.RING;
      (async function () {
        for (;;) {
          while (Atomics.load(ctl, 1) === done) Atomics.wait(ctl, 1, done);
          var slot = ctl[R0 + (done % RING) * 2], chunk = ctl[R0 + (done % RING) * 2 + 1];
          done++;
          var ok = 0;
          try {
            var r = await fetch(m.base + m.names[chunk]);
            if (r.ok) { var buf = new Uint8Array(await r.arrayBuffer()); data.set(buf, slot * m.chunkBytes); ok = 1; }
          } catch (err) {}
          Atomics.store(ctl, S0 + slot * 3, ok ? 2 : 3);
        }
      })();
    };
  }

  function mkdirs(path) { try { FS.mkdirTree(path); } catch (e) {} }

  Module.preRun = Module.preRun || [];
  Module.preRun.push(function () {
    addRunDependency('gamefs');
    fetch('data/manifest.json').then(function (r) { return r.json(); }).then(function (man) {
      chunkBytes = man.chunk_size;
      names = []; sizes = [];
      var files = man.files, first = [];
      files.forEach(function (f, fi) { first[fi] = names.length; f.chunks.forEach(function (c) { names.push(c.name); sizes.push(c.size); }); });
      ctl = new Int32Array(new SharedArrayBuffer((S0 + NSLOT * 3 + 16) * 4));
      var dataSab = new SharedArrayBuffer(NSLOT * chunkBytes);
      data = new Uint8Array(dataSab);
      var src = '(' + workerMain.toString() + ')()';
      var w = new Worker(URL.createObjectURL(new Blob([src], { type: 'text/javascript' })));
      w.postMessage({ ctl: ctl.buffer, data: dataSab, names: names, chunkBytes: chunkBytes, R0: R0, S0: S0, RING: RING,
                      base: new URL('data/', location.href).href });
      files.forEach(function (f, fi) {
        var p = '/Game Files/' + f.path, d = p.slice(0, p.lastIndexOf('/'));
        mkdirs(d);
        var size = f.size, c0 = first[fi];
        var lazy = { length: size, get: function () { throw new Error('gamefs: byte access'); } };
        var node = FS.createFile(d, p.slice(p.lastIndexOf('/') + 1), { isDevice: false, contents: lazy }, true, false);
        node.contents = lazy;
        Object.defineProperties(node, { usedBytes: { get: function () { return size; } } });
        function read(stream, buffer, offset, length, position) {
          if (position >= size) return 0;
          var n = Math.min(size - position, length), done = 0;
          while (done < n) {
            var pos = position + done, c = Math.floor(pos / chunkBytes), within = pos - c * chunkBytes;
            var take = Math.min(n - done, sizes[c0 + c] - within);
            readChunk(c0 + c, buffer, offset + done, within, take);
            done += take;
          }
          return n;
        }
        var ops = {};
        Object.keys(node.stream_ops).forEach(function (k) { ops[k] = node.stream_ops[k]; });
        ops.read = read;
        node.stream_ops = ops;
      });
      removeRunDependency('gamefs');
    });
  });
})();

// AudioWorklet output for platform/sdl/sdl_audio.c (the mixer thread fills a ring in shared memory).
Module.rtAudioInit = function (ring, frames, ctl, rate) {
        var buf = HEAPU8.buffer, c = new Int32Array(buf, ctl, 4);
        var ctx = new AudioContext({ sampleRate: rate, latencyHint: 'interactive' });
        var src = "class P extends AudioWorkletProcessor{constructor(o){super();var d=o.processorOptions;" +
          "this.r=new Float32Array(d.buf,d.ring,d.frames*2);this.c=new Int32Array(d.buf,d.ctl,4);this.n=d.frames;}" +
          "process(i,o){var L=o[0][0],R=o[0][1],rd=Atomics.load(this.c,1),wr=Atomics.load(this.c,0);" +
          "for(var k=0;k<L.length;k++){if(wr-rd>0){var j=(rd%this.n)*2;L[k]=this.r[j];R[k]=this.r[j+1];rd++;}else{L[k]=0;R[k]=0;}}" +
          "Atomics.store(this.c,1,rd);return true;}}registerProcessor('rt-audio',P);";
        ctx.audioWorklet.addModule(URL.createObjectURL(new Blob([src], { type: 'text/javascript' }))).then(function () {
          var n = new AudioWorkletNode(ctx, 'rt-audio', { outputChannelCount: [2],
            processorOptions: { buf: buf, ring: ring, frames: frames, ctl: ctl } });
          n.connect(ctx.destination);
        });
        ctx.onstatechange = function () { Atomics.store(c, 2, ctx.state === 'running' ? 1 : 0); };
        var resume = function () { if (ctx.state !== 'running') ctx.resume(); };
        ['keydown', 'pointerdown', 'mousedown', 'touchstart'].forEach(function (ev) { addEventListener(ev, resume); });
};

// Page errors reach the console with their stack (the shell only shows "Exception thrown").
(function () {
  if (typeof window === 'undefined') return;   // this file also runs in the pthread workers
  window.addEventListener('error', function (e) { console.warn('JS error:', e.message, e.filename + ':' + e.lineno, e.error && e.error.stack); });
  window.addEventListener('unhandledrejection', function (e) { console.warn('unhandled rejection:', e.reason && (e.reason.stack || e.reason)); });
})();
