package main

// A game catching up sends idle inputs ahead of the others' (runtime/core/net_frame.c), so nobody waits
// for it. A long load in its replay (no Present, so nothing sent: the boot's data and sound banks, a
// stadium) would stop the others and get it dropped as stalled. The server, which never blocks, sends
// them for it then: copies of its latest input (idle while it catches up) for the frames ahead, the same
// bytes the game sends for those frames once it runs again (dropped then: old frames).

import (
	"encoding/binary"
	"log"
	"time"
)

const (
	idleQuiet = 250 * time.Millisecond // a catching-up game that long without an input is loading
	idleAhead = 60                     // idle inputs this many frames past the others' front
)

// heardInput records slot k's input m for frame f; false for a frame the server sent for it already (the
// game's own copy: not relayed again).
func (r *room) heardInput(k int, m []byte, f int64) bool {
	if f < r.sent[k] {
		return false
	}
	if f > r.last[k] {
		r.last[k] = f
	}
	r.idle[k], r.heard[k] = m, time.Now()
	if r.filling[k] {
		r.filling[k] = false
		log.Printf("room %s: player %d sends its inputs again at frame %d", r.code, k+1, f)
	}
	return true
}

// fillIdle sends idle inputs for every catching-up game that has gone quiet, up to front (the furthest
// input of the games in step) + idleAhead.
func (r *room) fillIdle(front int64) {
	for k, p := range r.slots {
		if p == nil || !p.catching || r.from[k] < 0 || time.Since(r.heard[k]) < idleQuiet ||
			r.last[k] >= front+idleAhead/2 {
			continue
		}
		if r.last[k] < r.from[k] { // nothing sent since its grant (its boot loads): the idle input of a pad
			m := make([]byte, 19) // (plugged in, nothing pressed: net_frame.c send_input)
			m[0], m[1], m[17] = 'I', byte(k), 1
			r.idle[k], r.last[k] = m, r.from[k]-1
		}
		m := r.idle[k]
		if len(m) != 19 || m[18] != 0 {
			continue
		}
		if !r.filling[k] {
			r.filling[k] = true
			log.Printf("room %s: player %d quiet while catching up: idle inputs from frame %d", r.code, k+1, r.last[k]+1)
		}
		for f := r.last[k] + 1; f <= front+idleAhead; f++ {
			c := append([]byte(nil), m...)
			binary.LittleEndian.PutUint32(c[2:], uint32(f))
			r.relay(c, -1)
		}
		r.last[k], r.sent[k] = front+idleAhead, front+idleAhead+1
	}
	if len(r.forks) > 0 {
		r.tryForks()
	}
}
