package main

import (
	"strings"
	"sync"
	"testing"
)

// A page of another game build is refused (peek and join): lockstep needs the same game everywhere.
func TestOtherBuildRefused(t *testing.T) {
	host := &client{name: "h"}
	host.cond = sync.NewCond(&host.mu)
	host.handle(request{T: "host", Build: "A"})
	code := host.room.code
	defer delete(rooms, code)
	for _, tc := range []struct {
		t, build, want string
	}{{"peek", "B", `"ok":false`}, {"join", "B", "another version"}, {"peek", "A", `"ok":true`}, {"join", "A", `"t":"joined"`}} {
		c := &client{name: "f"}
		c.cond = sync.NewCond(&c.mu)
		c.handle(request{T: tc.t, Room: code, Build: tc.build})
		got := ""
		for _, m := range queued(c) {
			got += string(m)
		}
		if !strings.Contains(got, tc.want) {
			t.Errorf("%s with build %s: %s, want %s", tc.t, tc.build, got, tc.want)
		}
	}
}
