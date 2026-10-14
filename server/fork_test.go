package main

import (
	"bytes"
	"encoding/json"
	"testing"
)

func input(slot int, frame int64) []byte {
	return append(frameMsg('I', slot, frame), make([]byte, 13)...)
}

func lockstepRoom(code string) *room {
	r := &room{code: code}
	for k := range r.from {
		r.from[k], r.last[k], r.freed[k], r.granted[k] = -1, -1, -1, -1
	}
	rooms[code] = r
	return r
}

// A friend leaving keeps its game: a room of its own with the party's log and the others gone at its
// frame; the fork waits for the inputs it played; a newcomer in the party's freed slot is told "since".
func TestForkLeaverKeepsItsGame(t *testing.T) {
	r := lockstepRoom("PARTY1")
	host, friend := testClient(r, 0), testClient(r, 1)
	r.from[0], r.from[1] = 0, 5
	for f := int64(0); f <= 8; f++ {
		host.game(input(0, f))
	}
	for f := int64(5); f <= 12; f++ {
		friend.game(input(1, f))
	}
	queued(host)
	queued(friend)
	friend.handle(request{T: "fork", Frame: 10}) // its game played 0..9: the host's 9 is not here yet
	if friend.room != r || len(r.forks) != 1 {
		t.Fatal("the fork did not wait for the host's input 9")
	}
	host.game(input(0, 9))
	r2 := friend.room
	if r2 == r || r2 == nil || r2.slots[1] != friend || r2.auth != 1 || len(r.forks) != 0 {
		t.Fatal("the friend is not in a room of its own after the host's input 9")
	}
	want := append([]byte(nil), r2.log[:len(r2.log)-18]...)
	for _, k := range []int{0, 2, 3} {
		want = append(want, frameMsg('D', k, 10)...)
	}
	if !bytes.Equal(r2.log, want) || !bytes.HasPrefix(r2.log, input(0, 0)) {
		t.Error("the new room's log is not the party's log plus the others' 'D' at 10")
	}
	got := queued(friend) // the host's input 9, the 'D's, then {forked}
	if len(got) < 5 || !bytes.Equal(got[0], input(0, 9)) || got[1][0] != 'D' || got[2][0] != 'D' || got[3][0] != 'D' {
		t.Fatalf("the friend got %d messages, want input 9 and the 'D's first", len(got))
	}
	var m map[string]any
	if json.Unmarshal(got[4], &m) != nil || m["t"] != "forked" || m["room"] != r2.code {
		t.Errorf("then {forked}: got %s", got[4])
	}
	if r.slots[1] != nil || !bytes.HasSuffix(r.log, frameMsg('D', 1, 13)) || r.freed[1] != 13 {
		t.Error("the party did not get the friend's 'D' after its last input 12")
	}
	if r2.freed[0] != 10 || r2.from[1] != 5 || r2.last[1] != 12 {
		t.Errorf("own room: slot 1 freed at %d (want 10), the leaver from %d last %d", r2.freed[0], r2.from[1], r2.last[1])
	}
	delete(rooms, r.code)
	delete(rooms, r2.code)
}
