package main

import (
	"bytes"
	"encoding/binary"
	"sync"
	"testing"
)

func haMsg(t byte, slot int, frame uint32, payload []byte) []byte {
	m := make([]byte, haHeader, haHeader+len(payload))
	m[0], m[1] = t, byte(slot)
	binary.LittleEndian.PutUint32(m[2:], frame)
	binary.LittleEndian.PutUint16(m[6:], uint16(len(payload)))
	return append(m, payload...)
}

func testClient(r *room, slot int) *client {
	c := &client{room: r, slot: slot, name: "p"}
	c.cond = sync.NewCond(&c.mu)
	r.slots[slot] = c
	return c
}

func queued(c *client) [][]byte {
	c.mu.Lock()
	defer c.mu.Unlock()
	var out [][]byte
	for _, m := range c.queue {
		out = append(out, m.p)
	}
	c.queue, c.queued = nil, 0
	return out
}

func TestMsgLenHostMode(t *testing.T) {
	for _, ty := range []byte{'S', 'F', 'K', 'P', 'E', 'Z', 'U'} {
		if n := msgLen([]byte{ty, 0, 0}); n != haHeader {
			t.Errorf("%c short: %d, want %d", ty, n, haHeader)
		}
		if n := msgLen(haMsg(ty, 1, 7, make([]byte, 4652))); n != haHeader+4652 {
			t.Errorf("%c: %d, want %d", ty, n, haHeader+4652)
		}
	}
	// the old types are unchanged
	if msgLen([]byte{'H'}) != 14 || msgLen([]byte{'A'}) != 6 || msgLen([]byte{'W'}) != 2 || msgLen([]byte{'X'}) != 0 {
		t.Error("old message lengths changed")
	}
	in := make([]byte, 19)
	in[0], in[18] = 'I', 2
	if msgLen(in) != 19+12 {
		t.Error("'I' length changed")
	}
}

func TestHostModeRelay(t *testing.T) {
	r := &room{code: "T"}
	host, friend := testClient(r, 0), testClient(r, 1)
	setup := haMsg('S', 0, 100, []byte{1, 2, 3})
	key := haMsg('F', 0, 101, []byte{0, 9, 9})
	delta := haMsg('F', 0, 102, []byte{1, 5})
	in := frameMsg('H', 0, 102)
	in = append(in, make([]byte, 8)...)
	// one batch mixing old and new types
	host.game(bytes.Join([][]byte{setup, key, in, delta}, nil))
	got := queued(friend)
	want := [][]byte{setup, key, in, delta}
	if len(got) != len(want) {
		t.Fatalf("friend got %d messages, want %d", len(got), len(want))
	}
	for i := range want {
		if !bytes.Equal(got[i], want[i]) {
			t.Errorf("message %d: %v, want %v", i, got[i], want[i])
		}
	}
	if len(queued(host)) != 0 {
		t.Error("the sender got its own messages back")
	}
	if !bytes.Equal(r.log, in) {
		t.Errorf("log %v: only the 'H' belongs there", r.log)
	}
	if !bytes.Equal(r.setup, setup) || !bytes.Equal(r.key, key) {
		t.Error("setup / key frame not kept (a delta must not replace the key)")
	}
	// friends' pads and key requests reach the host; a message for another slot is dropped
	pad, kreq := haMsg('P', 1, 50, make([]byte, 12)), haMsg('K', 1, 50, nil)
	friend.game(append(append(append([]byte{}, pad...), kreq...), haMsg('P', 0, 50, nil)...))
	if got := queued(host); len(got) != 2 || !bytes.Equal(got[0], pad) || !bytes.Equal(got[1], kreq) {
		t.Errorf("host got %v", got)
	}
	// a newcomer: log, then setup, then key
	late := testClient(r, 2)
	r.dump(late)
	r.dumpHA(late)
	got = queued(late)
	if len(got) != 3 || !bytes.Equal(got[0], in) || !bytes.Equal(got[1], setup) || !bytes.Equal(got[2], key) {
		t.Errorf("newcomer got %v", got)
	}
}

func TestHostModeEndForgetsMatch(t *testing.T) {
	r := &room{code: "E"}
	host, friend := testClient(r, 0), testClient(r, 1)
	host.game(append(haMsg('S', 0, 1, []byte{1}), haMsg('F', 0, 2, []byte{0, 9})...))
	host.game(haMsg('E', 0, 3, nil))
	if r.setup != nil || r.key != nil {
		t.Fatal("setup or key kept after the match ended")
	}
	if got := queued(friend); len(got) != 3 || got[2][0] != 'E' {
		t.Fatalf("friend got %d messages, want S, F, E", len(got))
	}
	late := testClient(r, 2)
	r.dumpHA(late)
	if len(queued(late)) != 0 {
		t.Fatal("a newcomer was sent the ended match")
	}
}
