package main

import (
	"net/http"
	"net/http/httptest"
	"testing"
)

func gated(g gate) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if g.allow(w, r) {
			_, _ = w.Write([]byte("game"))
		}
	})
}

func get(h http.Handler, path string, c *http.Cookie) *httptest.ResponseRecorder {
	r := httptest.NewRequest("GET", path, nil)
	if c != nil {
		r.AddCookie(c)
	}
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	return w
}

func TestGatePrivate(t *testing.T) {
	h := gated(gate{password: "scrum-42"})
	for _, p := range []string{"/", "/index.html", "/rugby08.wasm", "/data/manifest.json", "/ws"} {
		if w := get(h, p, nil); w.Code != http.StatusForbidden {
			t.Fatalf("%s without the link: %d, want 403", p, w.Code)
		}
	}
	if w := get(h, "/k/wrong", nil); w.Code != http.StatusForbidden || len(w.Result().Cookies()) != 0 {
		t.Fatalf("a wrong link: %d, cookies %v", w.Code, w.Result().Cookies())
	}
	w := get(h, "/k/scrum-42", nil)
	if w.Code != http.StatusSeeOther || w.Header().Get("Location") != "/" {
		t.Fatalf("the party link: %d to %q, want 303 to /", w.Code, w.Header().Get("Location"))
	}
	cs := w.Result().Cookies()
	if len(cs) != 1 || cs[0].Value == "scrum-42" {
		t.Fatalf("the party link's cookie: %v (must not be the password itself)", cs)
	}
	for _, p := range []string{"/", "/rugby08.wasm", "/ws"} {
		if w := get(h, p, cs[0]); w.Code != http.StatusOK || w.Body.String() != "game" {
			t.Fatalf("%s with the cookie: %d", p, w.Code)
		}
	}
	other := gated(gate{password: "another"})
	if w := get(other, "/", cs[0]); w.Code != http.StatusForbidden {
		t.Fatalf("another party's cookie let in: %d", w.Code)
	}
	r := httptest.NewRequest("GET", "/", nil)
	r.SetBasicAuth("x", "scrum-42")
	w = httptest.NewRecorder()
	h.ServeHTTP(w, r)
	if w.Code != http.StatusOK {
		t.Fatalf("basic auth with the password: %d", w.Code)
	}
}

func TestGatePublic(t *testing.T) {
	if w := get(gated(gate{}), "/", nil); w.Code != http.StatusOK {
		t.Fatalf("no password (public): %d", w.Code)
	}
}
