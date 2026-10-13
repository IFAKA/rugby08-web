// rtc.js (emcc --pre-js) -- party traffic peer to peer (pt.webrtc): each friend's browser and the
// host's are joined by a WebRTC connection with three data channels: "rel" (ordered, reliable: the
// match setup, its end, pause, key requests), "frames" (reliable but unordered: match frames, which are
// deltas of each other, so a loss costs one retransmission instead of a stall until a key (measured:
// ~12 frames per loss); nothing waits behind them, net_ha_rx.c puts them back in order) and "fast"
// (unordered, never retransmitted: pads, where only the newest matters).
// The server (server/signal.go) passes the offers, answers and ICE candidates, answers STUN itself, and
// relays game messages as before for whoever has no open channel: a message goes over the channels
// only when every peer it is for has one open, else over the relay (to everyone), never both.
// Module.rugbyNet (net_web.c) is the WebSocket; received channel messages join its queue.
// Test switches only: ?rtc=0 (the relay alone), ?rtcloss=P (the sender drops a share P of the frames: the
// friend's recovery, a key request, runs).

Module.rtc = (function () {
  const R = { me: -1, host: false, members: [], peers: {}, ice: [], off: false };
  R.stats = { dc: 0, ws: 0, rx: 0, open: 0 };
  const q = typeof location !== 'undefined' ? new URLSearchParams(location.search) : new URLSearchParams();
  R.off = q.get('rtc') === '0';
  R.loss = parseFloat(q.get('rtcloss') || '0'); // test switch: drop this share of the frames

  function signal(to, data) {
    const n = Module.rugbyNet;
    if (n && n.open && !n.closed) n.ws.send(JSON.stringify({ t: 'signal', to: to, data: data }));
  }

  function wire(p, slot, ch) {
    p[ch.label] = ch;
    ch.binaryType = 'arraybuffer';
    ch.onmessage = function (ev) {
      if (!Module.rugbyNet) return;
      Module.rugbyNet.queue.push(new Uint8Array(ev.data));
      R.stats.rx++;
    };
    ch.onopen = function () {
      const all = [p.rel, p.frames, p.fast];
      if (all.every((c) => c && c.readyState === 'open') && !p.open) {
        p.open = true;
        R.stats.open++;
        console.log('ms: rtc open with player ' + (slot + 1));
      }
    };
    ch.onclose = function () {
      p.open = false;
    };
  }

  function close(slot) {
    const p = R.peers[slot];
    if (!p) return;
    p.open = false;
    p.pc.close();
    delete R.peers[slot];
    console.log('ms: rtc closed with player ' + (slot + 1));
  }

  function peer(slot, offerer) {
    close(slot);
    const pc = new RTCPeerConnection({ iceServers: R.ice });
    const p = { pc: pc, open: false, cands: [] };
    R.peers[slot] = p;
    pc.onicecandidate = function (e) {
      if (e.candidate) signal(slot, { cand: e.candidate.toJSON() });
    };
    pc.onconnectionstatechange = function () {
      if (pc.connectionState === 'failed' || pc.connectionState === 'closed') p.open = false;
    };
    if (offerer) {
      wire(p, slot, pc.createDataChannel('rel', { ordered: true }));
      wire(p, slot, pc.createDataChannel('frames', { ordered: false }));
      wire(p, slot, pc.createDataChannel('fast', { ordered: false, maxRetransmits: 0 }));
      pc.createOffer()
        .then((o) => pc.setLocalDescription(o))
        .then(() => signal(slot, { sdp: pc.localDescription.toJSON() }));
    } else {
      pc.ondatachannel = function (e) {
        wire(p, slot, e.channel);
      };
    }
    return p;
  }

  function onSignal(from, d) {
    let p = R.peers[from];
    if (d.sdp && d.sdp.type === 'offer') {
      p = peer(from, false);
      p.pc
        .setRemoteDescription(d.sdp)
        .then(() => Promise.all(p.cands.map((c) => p.pc.addIceCandidate(c))))
        .then(() => p.pc.createAnswer())
        .then((a) => p.pc.setLocalDescription(a))
        .then(() => signal(from, { sdp: p.pc.localDescription.toJSON() }));
      return;
    }
    if (!p) return;
    if (d.sdp) p.pc.setRemoteDescription(d.sdp).then(() => p.cands.forEach((c) => p.pc.addIceCandidate(c)));
    else if (d.cand && p.pc.remoteDescription) p.pc.addIceCandidate(d.cand);
    else if (d.cand) p.cands.push(d.cand);
  }

  // Lobby JSON from the server (net_web.c): true when it was only for this layer.
  R.text = function (m) {
    if (m.t === 'joined') {
      R.me = m.slot;
      R.host = !!m.creator;
      R.ice = m.stun ? [{ urls: 'stun:' + location.hostname + ':' + m.stun }] : [];
      if (!R.host && !R.off) peer(0, true); // a friend calls the host
    } else if (m.t === 'members') {
      R.members = (m.players || []).map((p) => p.slot).filter((s) => s !== R.me);
      Object.keys(R.peers).forEach((s) => {
        if (R.members.indexOf(+s) < 0 && !(+s === 0 && !R.host)) close(+s);
      });
    } else if (m.t === 'signal') {
      if (!R.off) onSignal(m.from, m.data || {});
      return true;
    }
    return false;
  };

  // A binary game message ({u8 type, u8 slot, u32 frame, u16 len, payload}, net_ha.c): true when it
  // went over the data channels (every peer it is for has them open).
  R.send = function (bytes) {
    if (R.off || R.me < 0) return false;
    const label = bytes[0] === 70 ? 'frames' : bytes[0] === 80 ? 'fast' : 'rel'; // 'F' frame, 'P' pad
    const to = R.host ? R.members : [0];
    if (!to.length) return false;
    const chans = [];
    for (const s of to) {
      const p = R.peers[s];
      const ch = p && p.open && p[label];
      if (!ch || ch.readyState !== 'open') return false;
      chans.push(ch);
    }
    if (label === 'frames' && R.loss && Math.random() < R.loss) {
      R.stats.dropped = (R.stats.dropped | 0) + 1;
      return true;
    }
    for (const ch of chans) if (label !== 'fast' || ch.bufferedAmount < 1 << 16) ch.send(bytes); // (a congested pad channel drops)
    R.stats.dc++;
    return true;
  };

  R.leave = function () {
    Object.keys(R.peers).forEach((s) => close(+s));
    R.me = -1;
    R.members = [];
  };
  return R;
})();
