// fork.go -- a player leaving a lockstep party keeps playing its own game (runtime/core/net_leave.c).
// Its game holds the party's whole session: it gets a room of its own whose log is the party's log so far
// plus 'D' for every other slot at frame G (the first frame the leaver's game plays without them), the
// same 'D's it is sent, so a friend joining it later replays exactly the game the leaver has. The party
// goes on without the leaver (its 'D' after its last input, as for any leave).
//
//	-> {"t":"fork","frame":G}   <- 'D' k G for every other slot, then {"t":"forked","room":C2}
//
// The leaver's game has played every frame before G with the others' inputs, and some may have come peer
// to peer and not have reached the server yet: the fork waits until the log has them (or that player left).
package main

import "log"

type forkReq struct {
	c     *client
	frame int64
}

// forkReady: the log has every playing slot's inputs before the fork's frame.
func (r *room) forkReady(f forkReq) bool {
	for k, p := range r.slots {
		if k != f.c.slot && p != nil && r.from[k] >= 0 && r.from[k] < f.frame && r.last[k] < f.frame-1 {
			return false
		}
	}
	return true
}

// tryForks: the forks waiting for inputs, done once the log has them (after an input, after a leave).
func (r *room) tryForks() {
	for i := 0; i < len(r.forks); {
		f := r.forks[i]
		switch {
		case f.c.room != r || r.slots[f.c.slot] != f.c:
			r.forks = append(r.forks[:i], r.forks[i+1:]...)
		case r.forkReady(f):
			r.forks = append(r.forks[:i], r.forks[i+1:]...)
			r.fork(f)
		default:
			i++
		}
	}
}

func (r *room) fork(f forkReq) {
	c, s := f.c, f.c.slot
	r2 := &room{build: r.build, auth: s, log: append([]byte(nil), r.log...)}
	for r2.code = newCode(); rooms[r2.code] != nil; r2.code = newCode() {
	}
	for k := range r2.from {
		r2.from[k], r2.last[k], r2.freed[k], r2.granted[k] = -1, -1, -1, r.granted[k]
		if k == s {
			continue
		}
		d := frameMsg('D', k, f.frame)
		r2.log = append(r2.log, d...)
		c.enqueue(2, d)
		r2.freed[k] = f.frame // (a newcomer there later: the slot's 'A's and 'D's so far are not its own)
		if r.granted[k] >= f.frame {
			r2.freed[k] = r.granted[k] + 1
		}
	}
	r2.from[s], r2.last[s] = r.from[s], r.last[s]
	r.drop(c) // the party goes on without it
	c.room, r2.slots[s] = r2, c
	rooms[r2.code] = r2
	c.send(map[string]any{"t": "forked", "room": r2.code})
	log.Printf("room %s: %s (player %d) left and plays on in its own room %s from frame %d", r.code, c.name, s+1,
		r2.code, f.frame)
	r2.members()
}
