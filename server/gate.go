package main

// gate.go: the private party site (the user, 2026-10-19). With a password every request needs the party's
// cookie, which the party link https://host/k/<password> sets: a friend only opens the link it was sent (no
// prompt, no account). Without the cookie a short page says to ask a friend for the link. HTTP basic auth
// with the password is still accepted (scripts). No password: the site is public.

import (
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"net/http"
	"strings"
)

const gateCookie = "rugby08_party"

type gate struct{ password string }

func (g gate) token() string {
	h := sha256.Sum256([]byte("rugby08 party " + g.password))
	return hex.EncodeToString(h[:])
}

func same(a, b string) bool { return subtle.ConstantTimeCompare([]byte(a), []byte(b)) == 1 }

// allow reports whether r may go on; if not, it has answered (the link's redirect or the refusal page).
func (g gate) allow(w http.ResponseWriter, r *http.Request) bool {
	if g.password == "" {
		return true
	}
	if k, ok := strings.CutPrefix(r.URL.Path, "/k/"); ok {
		if !same(k, g.password) {
			refuse(w, "This party link is not right. Ask the friend who sent it for the link again.")
			return false
		}
		http.SetCookie(w, &http.Cookie{
			Name: gateCookie, Value: g.token(), Path: "/", MaxAge: 400 * 24 * 3600,
			HttpOnly: true, Secure: r.TLS != nil, SameSite: http.SameSiteLaxMode,
		})
		http.Redirect(w, r, "/", http.StatusSeeOther)
		return false
	}
	if c, err := r.Cookie(gateCookie); err == nil && same(c.Value, g.token()) {
		return true
	}
	if _, p, ok := r.BasicAuth(); ok && same(p, g.password) {
		return true
	}
	refuse(w, "This is a private Rugby 08 site. Open the party link a friend sent you.")
	return false
}

func refuse(w http.ResponseWriter, msg string) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.WriteHeader(http.StatusForbidden)
	_, _ = w.Write([]byte(`<!doctype html><meta name="viewport" content="width=device-width"><title>Rugby 08</title>` +
		`<body style="margin:0;height:100vh;display:flex;align-items:center;justify-content:center;background:#000;` +
		`color:#eef5f0;font:18px/1.5 system-ui,sans-serif;text-align:center;padding:24px">` + msg + `</body>`))
}
