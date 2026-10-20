package main

import (
	"encoding/binary"
	"testing"
	"time"
)

func idleRoom() (*room, *client, *client) {
	r := &room{code: "TEST"}
	for k := range r.from {
		r.from[k], r.last[k], r.freed[k], r.granted[k] = -1, -1, -1, -1
	}
	host, joiner := testClient(r, 0), testClient(r, 1)
	host.caughtUp = true
	joiner.catching = true
	r.from[0], r.from[1] = 0, 1000
	return r, host, joiner
}

func idleInput(slot int, f uint32) []byte {
	m := make([]byte, 19)
	m[0], m[1] = 'I', byte(slot)
	binary.LittleEndian.PutUint32(m[2:], f)
	m[17] = 1 // (the pad's spare: plugged in)
	return m
}

// A catching-up game gone quiet (a load in its replay) gets its idle inputs from the server, copies of
// its latest; its own later ones for those frames are not relayed again.
func TestIdleFillQuiet(t *testing.T) {
	r, host, _ := idleRoom()
	r.heardInput(0, idleInput(0, 1200), 1200)
	r.heardInput(1, idleInput(1, 1210), 1210)
	queued(host)
	r.fillIdle(1200)
	if n := len(queued(host)); n != 0 {
		t.Fatalf("filled for a game that is not quiet: %d messages", n)
	}
	r.heard[1] = time.Now().Add(-time.Second)
	r.fillIdle(1200)
	got := queued(host)
	if len(got) != 50 || r.last[1] != 1260 {
		t.Fatalf("filled %d messages up to %d, want 50 up to 1260", len(got), r.last[1])
	}
	for i, m := range got {
		want := idleInput(1, uint32(1211+i))
		if string(m) != string(want) {
			t.Fatalf("message %d: % x, want % x", i, m, want)
		}
	}
	if r.heardInput(1, idleInput(1, 1230), 1230) {
		t.Fatal("the game's own input for a frame the server sent was relayed again")
	}
	if !r.heardInput(1, idleInput(1, 1261), 1261) || r.filling[1] {
		t.Fatal("the game's next input was not taken")
	}
}

// Games in step (and native ones, which never say they catch up) are never filled for.
func TestIdleFillOnlyCatchingUp(t *testing.T) {
	r, host, joiner := idleRoom()
	joiner.catching = false
	r.heardInput(1, idleInput(1, 1210), 1210)
	r.heard[1] = time.Now().Add(-time.Second)
	queued(host)
	r.fillIdle(1200)
	if n := len(queued(host)); n != 0 {
		t.Fatalf("filled for a game in step: %d messages", n)
	}
}

// A game quiet from its grant on (its boot loads at once) gets a plugged-in pad's idle inputs.
func TestIdleFillFromGrant(t *testing.T) {
	r, host, _ := idleRoom()
	r.heardInput(0, idleInput(0, 1200), 1200)
	queued(host)
	r.fillIdle(1200)
	got := queued(host)
	if len(got) != 261 || string(got[0]) != string(idleInput(1, 1000)) || r.last[1] != 1260 {
		t.Fatalf("filled %d messages up to %d, want 261 from frame 1000 up to 1260", len(got), r.last[1])
	}
	r.heardInput(0, idleInput(0, 1300), 1300)
	r.fillIdle(1300)
	if got = queued(host); len(got) != 100 || string(got[0]) != string(idleInput(1, 1261)) {
		t.Fatalf("filled %d messages again, want 100 from frame 1261", len(got))
	}
}
