// peers.js (emcc --pre-js) -- a lockstep party's inputs peer to peer (ls.lag). Every two players' browsers
// are joined by a WebRTC connection (a mesh: at most 4 players) with one data channel, unordered and never
// retransmitted: each input ('I') and state hash ('H') goes to every peer at once, and ALSO through the
// server, which keeps the room's log (joiners replay it) and relays it to everyone: a message lost on the
// way peer to peer still arrives by the relay, a little later. The game takes a peer's copy only for a
// frame it has not played yet and not too far ahead (runtime/core/netplay.c net_pump), so the copies
// never get in the way of the relay's ordered stream.
// The channel also measures each peer's round trip (pings every 500 ms): the input delay the page picks
// (shell.html rugbyNetDelay) covers the slowest peer's trip and its jitter.
// The server (server/signal.go) passes the offers, answers and ICE candidates and answers STUN itself.
// Test switches: ?rtc=0 (the relay alone), ?netlag=MS[,JITTER] (this page holds back everything it sends,
// peer to peer and to the server, and what the server relays to it, for MS +- JITTER ms: a slow network
// on one machine).

Module.peers = (function () {
  const P = { me: -1, members: [], conns: {}, ice: [], inq: [], stats: { tx: 0, rx: 0, used: 0 } };
  const q = typeof location !== 'undefined' ? new URLSearchParams(location.search) : new URLSearchParams();
  const off = q.get('rtc') === '0';
  const lag = (q.get('netlag') || '').split(',').map(Number);
  P.lag = lag[0] > 0 ? { ms: lag[0], jitter: lag[1] || 0 } : null;

  // a test's slow network: everything this page sends leaves later (ordered: the server's socket keeps
  // its order, as TCP would), and what the server relays to it arrives later too: a relayed message
  // crosses the network twice (sender to server, server to this page), a peer's once.
  // An ordered direction is a queue drained by one timer: a timer each would not keep the order (each
  // delay is cut to whole ms from when it was set, so of two messages due at once the later may run first).
  const fifo = { out: [], in: [] };
  function drain(q) {
    while (q.length && q[0].t <= performance.now()) q.shift().fn();
    if (q.length) setTimeout(() => drain(q), Math.max(0, q[0].t - performance.now()));
  }
  function hold(fn, ordered, dir) {
    if (!P.lag) return fn();
    const t = performance.now() + Math.max(0, P.lag.ms + (Math.random() * 2 - 1) * P.lag.jitter);
    if (!ordered) return void setTimeout(fn, t - performance.now());
    const q = fifo[dir];
    q.push({ t: q.length ? Math.max(q[q.length - 1].t, t) : t, fn });
    if (q.length === 1) setTimeout(() => drain(q), t - performance.now());
  }
  P.later = (fn, ordered) => hold(fn, ordered, 'out');
  P.relayed = (fn) => hold(fn, true, 'in');

  function signal(to, data) {
    const n = Module.rugbyNet;
    if (n && n.ws.readyState === 1) n.ws.send(JSON.stringify({ t: 'signal', to: to, data: data }));
  }

  function wire(c, slot, ch) {
    c.ch = ch;
    ch.binaryType = 'arraybuffer';
    ch.onopen = function () {
      c.open = true;
      console.log('ms: peer open with player ' + (slot + 1));
    };
    ch.onclose = function () {
      c.open = false;
    };
    ch.onmessage = function (ev) {
      const u8 = new Uint8Array(ev.data);
      if (u8[0] === 0xfe) {
        // a ping: back at once (marked if this page was just blocked: it waited here, not on the way)
        const b = new Uint8Array(10);
        b.set(u8.subarray(0, 9));
        b[0] = 0xff;
        b[9] = awake() ? 0 : 1;
        P.later(() => c.open && c.ch.send(b));
      } else if (u8[0] === 0xff) {
        // our ping back: the round trip, unless a page was blocked on the way
        const sent = new DataView(u8.buffer).getFloat64(1, true);
        if (!awake() || u8[9] || woke >= sent) return;
        const r = performance.now() - sent;
        c.jit = c.rtt === undefined ? 0 : c.jit * 0.8 + Math.abs(r - c.rtt) * 0.2;
        c.rtt = c.rtt === undefined ? r : c.rtt * 0.8 + r * 0.2;
      } else {
        P.inq.push(u8);
        P.stats.rx++;
      }
    };
  }

  function close(slot) {
    const c = P.conns[slot];
    if (!c) return;
    c.open = false;
    c.pc.close();
    delete P.conns[slot];
    console.log('ms: peer closed with player ' + (slot + 1));
  }

  function connect(slot, offerer) {
    close(slot);
    const pc = new RTCPeerConnection({ iceServers: P.ice });
    const c = { pc: pc, open: false, cands: [] };
    P.conns[slot] = c;
    pc.onicecandidate = (e) => e.candidate && signal(slot, { cand: e.candidate.toJSON() });
    pc.onconnectionstatechange = () => {
      if (pc.connectionState === 'failed' || pc.connectionState === 'closed') c.open = false;
    };
    if (offerer) {
      wire(c, slot, pc.createDataChannel('inputs', { ordered: false, maxRetransmits: 0 }));
      pc.createOffer()
        .then((o) => pc.setLocalDescription(o))
        .then(() => signal(slot, { sdp: pc.localDescription.toJSON() }));
    } else pc.ondatachannel = (e) => wire(c, slot, e.channel);
    return c;
  }

  function onSignal(from, d) {
    let c = P.conns[from];
    if (d.sdp && d.sdp.type === 'offer') {
      c = connect(from, false);
      c.pc
        .setRemoteDescription(d.sdp)
        .then(() => Promise.all(c.cands.map((x) => c.pc.addIceCandidate(x))))
        .then(() => c.pc.createAnswer())
        .then((a) => c.pc.setLocalDescription(a))
        .then(() => signal(from, { sdp: c.pc.localDescription.toJSON() }));
      return;
    }
    if (!c) return;
    if (d.sdp) c.pc.setRemoteDescription(d.sdp).then(() => c.cands.forEach((x) => c.pc.addIceCandidate(x)));
    else if (d.cand && c.pc.remoteDescription) c.pc.addIceCandidate(d.cand);
    else if (d.cand) c.cands.push(d.cand);
  }

  // The server's JSON (shell.html): true when it was only for this layer.
  P.text = function (m) {
    if (off) return m.t === 'signal';
    if (m.t === 'joined') {
      P.me = m.slot;
      P.ice = m.stun ? [{ urls: 'stun:' + location.hostname + ':' + m.stun }] : [];
    } else if (m.t === 'members') {
      // the lower slot of each pair calls the higher
      P.members = (m.players || []).map((p) => p.slot).filter((s) => s !== P.me);
      P.members.forEach((s) => P.me < s && !P.conns[s] && connect(s, true));
      Object.keys(P.conns).forEach((s) => P.members.indexOf(+s) < 0 && close(+s));
    } else if (m.t === 'signal') {
      onSignal(m.from, m.data || {});
      return true;
    }
    return false;
  };

  // A game message: inputs and hashes also go peer to peer (the relay gets every message regardless).
  P.send = function (bytes) {
    if (bytes[0] !== 73 && bytes[0] !== 72) return; // 'I', 'H'
    for (const s of P.members) {
      const c = P.conns[s];
      if (c && c.open && c.ch.bufferedAmount < 1 << 16) {
        P.later(() => c.open && c.ch.send(bytes));
        P.stats.tx++;
      }
    }
  };

  // The one-way trip to the slowest peer, its jitter included (ms), or null while some peer has no
  // channel or no measure yet (the page then goes by the server's round trips).
  P.oneWay = function () {
    let worst = 0;
    for (const s of P.members) {
      const c = P.conns[s];
      if (!c || !c.open || c.rtt === undefined) return null;
      worst = Math.max(worst, c.rtt / 2 + c.jit); // (the trip and one jitter: the pings already count each page's wait to be handled)
    }
    return P.members.length ? worst : null;
  };

  // A page blocked (a load: no yield for a second or more) answers its pings late. That is no network
  // trip, and counted as one it would raise the input delay to the cap for the next seconds.
  let beat = performance.now(),
    woke = -1e9;
  function awake() {
    const t = performance.now();
    if (t - beat > 150) woke = t;
    beat = t;
    return t - woke > 200;
  }
  setInterval(awake, 50);

  setInterval(() => {
    for (const s of P.members) {
      const c = P.conns[s];
      if (!c || !c.open) continue;
      const b = new Uint8Array(9);
      b[0] = 0xfe;
      new DataView(b.buffer).setFloat64(1, performance.now(), true);
      P.later(() => c.open && c.ch.send(b));
    }
  }, 500);
  return P;
})();
