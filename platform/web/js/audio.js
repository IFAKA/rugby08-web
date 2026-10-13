// audio.js (emcc --pre-js) -- AudioWorklet output for platform/sdl/sdl_audio.c: the game's mixer thread
// fills a ring in shared memory (ctl: [0] frames written, [1] frames read, [2] 1 while the context
// runs) and this worklet drains it. Browsers keep the AudioContext suspended until a user gesture.
// More than twice the target latency waiting (written while nothing read: before the first gesture,
// a stalled device) is dropped, not played late: the mixer's clock is the game's DirectSound cursor.

// Runs in the AudioWorkletGlobalScope (passed as source text).
function rtAudioWorklet() {
  class RingPlayer extends AudioWorkletProcessor {
    constructor(o) {
      super();
      const d = o.processorOptions;
      this.ring = new Float32Array(d.buf, d.ring, d.frames * 2);
      this.ctl = new Int32Array(d.buf, d.ctl, 4);
      this.frames = d.frames;
      this.target = d.target;
    }
    process(inputs, outputs) {
      const left = outputs[0][0];
      const right = outputs[0][1];
      let rd = Atomics.load(this.ctl, 1);
      const wr = Atomics.load(this.ctl, 0);
      if (wr - rd > 2 * this.target) rd = wr - this.target;
      for (let k = 0; k < left.length; k++) {
        if (wr - rd > 0) {
          const j = (rd % this.frames) * 2;
          left[k] = this.ring[j];
          right[k] = this.ring[j + 1];
          rd++;
        } else {
          left[k] = 0; // underrun: silence
          right[k] = 0;
        }
      }
      Atomics.store(this.ctl, 1, rd);
      return true;
    }
  }
  registerProcessor('rt-audio', RingPlayer);
}

Module.rtAudioInit = function (ring, frames, ctl, rate, target) {
  const buf = HEAPU8.buffer;
  const c = new Int32Array(buf, ctl, 4);
  const ctx = new AudioContext({ sampleRate: rate, latencyHint: 'interactive' });
  const src = '(' + rtAudioWorklet.toString() + ')()';
  ctx.audioWorklet.addModule(URL.createObjectURL(new Blob([src], { type: 'text/javascript' }))).then(function () {
    const n = new AudioWorkletNode(ctx, 'rt-audio', {
      outputChannelCount: [2],
      processorOptions: { buf: buf, ring: ring, frames: frames, ctl: ctl, target: target },
    });
    n.connect(ctx.destination);
  });
  ctx.onstatechange = function () {
    Atomics.store(c, 2, ctx.state === 'running' ? 1 : 0);
  };
  const resume = function () {
    if (ctx.state !== 'running') ctx.resume();
  };
  ['keydown', 'pointerdown', 'mousedown', 'touchstart'].forEach(function (ev) {
    addEventListener(ev, resume);
  });
};
