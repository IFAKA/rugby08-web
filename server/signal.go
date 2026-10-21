package main

// WebRTC signaling (pt.webrtc): party traffic goes peer to peer between browsers (each friend and the
// host, over two data channels; platform/web/js/rtc.js) and this server only passes on the peers'
// offers, answers and ICE candidates, and relays game messages for peers without a direct channel.
//
//	-> {"t":"signal","to":S,"data":D}   <- {"t":"signal","from":S,"data":D}   (D: {sdp} or {cand})
//
// "joined" carries "stun":PORT when the server answers STUN (stun.go): a browser behind NAT learns its
// public address from this server, no third party.

var stunPort int // -stun: the UDP port of stun.go's responder (0: none)

func (c *client) signal(req request) {
	if c.room == nil || req.To == nil || *req.To < 0 || *req.To >= maxPlayers || *req.To == c.slot {
		return
	}
	if p := c.room.slots[*req.To]; p != nil {
		p.send(map[string]any{"t": "signal", "from": c.slot, "data": req.Data})
	}
}
