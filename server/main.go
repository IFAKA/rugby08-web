// Rugby 08 online relay: serves the web build and runs the rooms: drop-in lockstep play between friends.
// Standard library only (WebSocket implemented here). One process, no external services.
//
//	go run ./server -addr :8090 -dir build/web [-password SECRET] [-cert FULLCHAIN.pem -key KEY.pem]
//
// A room is one running game: its creator plays from the first frame; friends join at any time (up to
// 4 players, the game's controller limit). The server keeps the room's log of game messages and
// replays it to a newcomer, whose game catches up from boot (runtime/core/netplay.c).
//
// Lobby messages (JSON text):
//
//	-> {"t":"host","name":N}                         create a room (you are player 1)
//	-> {"t":"join","room":C,"name":N[,"auto":true]}  join (auto: create C if missing; tests)
//	<- {"t":"joined","room":C,"slot":S,"creator":bool}  then the room's log, then live messages
//	<- {"t":"members","room":C,"players":[{slot,name,rtt,joining,pct}]}
//	-> {"t":"ping","ts":X}  <- {"t":"pong","ts":X}    -> {"t":"rtt","rtt":MS}
//	-> {"t":"name","name":N}                          -> {"t":"progress","pct":P}  (catching up)
//	<- {"t":"error","msg":M}
//
// Game messages (binary, netplay.c): 'I' inputs and 'H' state hashes from each player and 'A' join
// grants from the room's authority are logged and relayed in one order to everyone else; the server
// adds 'D' when a player leaves (after that player's last input), sends 'W' (a newcomer waits) to the
// authority and 'R' to a player that becomes the authority when the previous one leaves.
package main

import (
	"bufio"
	"crypto/rand"
	"crypto/sha1"
	"crypto/subtle"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"errors"
	"flag"
	"io"
	"log"
	"net"
	"net/http"
	"strings"
	"sync"
)

const maxPlayers = 4

type outMsg struct {
	op int
	p  []byte
}

type client struct {
	conn     net.Conn
	mu       sync.Mutex // the outgoing queue
	cond     *sync.Cond
	queue    []outMsg
	queued   int
	closed   bool
	room     *room
	slot     int
	name     string
	rtt      int // ms, as the client last measured it
	pct      int // catching up: percent done
	caughtUp bool
}

type room struct {
	code    string
	slots   [maxPlayers]*client
	auth    int               // slot that grants joins
	log     []byte            // every game message since frame 0, in relay order
	from    [maxPlayers]int64 // slot plays from this frame (-1: not granted)
	last    [maxPlayers]int64 // last input frame received from the slot
	pending [maxPlayers]bool  // waiting for a grant
}

var (
	roomsMu sync.Mutex // rooms and everything in them; held while queueing, so all players see one order
	rooms   = map[string]*room{}
)

func msgLen(t byte) int {
	switch t {
	case 'I':
		return 18
	case 'H':
		return 14
	case 'A', 'D':
		return 6
	case 'W', 'R':
		return 2
	}
	return 0
}

// ---- WebSocket (RFC 6455), server side ---------------------------------------------------------

func upgrade(w http.ResponseWriter, r *http.Request) (net.Conn, *bufio.Reader, error) {
	if !strings.EqualFold(r.Header.Get("Upgrade"), "websocket") {
		return nil, nil, errors.New("not a websocket request")
	}
	h := sha1.Sum([]byte(r.Header.Get("Sec-WebSocket-Key") + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
	hj, ok := w.(http.Hijacker)
	if !ok {
		return nil, nil, errors.New("no hijacker")
	}
	conn, rw, err := hj.Hijack()
	if err != nil {
		return nil, nil, err
	}
	resp := "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
		base64.StdEncoding.EncodeToString(h[:]) + "\r\n\r\n"
	if _, err := conn.Write([]byte(resp)); err != nil {
		conn.Close()
		return nil, nil, err
	}
	return conn, rw.Reader, nil
}

// readMessage returns one complete message (opcode 1 text, 2 binary); control frames are handled.
func (c *client) readMessage(br *bufio.Reader) (int, []byte, error) {
	var msg []byte
	op := 0
	for {
		var h [2]byte
		if _, err := io.ReadFull(br, h[:]); err != nil {
			return 0, nil, err
		}
		fin, opcode, masked := h[0]&0x80 != 0, int(h[0]&0x0F), h[1]&0x80 != 0
		n := uint64(h[1] & 0x7F)
		if n == 126 {
			var b [2]byte
			if _, err := io.ReadFull(br, b[:]); err != nil {
				return 0, nil, err
			}
			n = uint64(binary.BigEndian.Uint16(b[:]))
		} else if n == 127 {
			var b [8]byte
			if _, err := io.ReadFull(br, b[:]); err != nil {
				return 0, nil, err
			}
			n = binary.BigEndian.Uint64(b[:])
		}
		if n > 1<<20 {
			return 0, nil, errors.New("message too large")
		}
		var key [4]byte
		if masked {
			if _, err := io.ReadFull(br, key[:]); err != nil {
				return 0, nil, err
			}
		}
		p := make([]byte, n)
		if _, err := io.ReadFull(br, p); err != nil {
			return 0, nil, err
		}
		if masked {
			for i := range p {
				p[i] ^= key[i&3]
			}
		}
		switch opcode {
		case 8:
			return 0, nil, io.EOF
		case 9:
			c.enqueue(10, p)
			continue
		case 10:
			continue
		case 0:
		default:
			op = opcode
		}
		msg = append(msg, p...)
		if fin {
			return op, msg, nil
		}
	}
}

// ---- outgoing: one ordered queue and one writer goroutine per client ------------------------------

const maxQueued = 64 << 20 // a client this far behind is dropped

func (c *client) enqueue(op int, p []byte) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.closed {
		return
	}
	if c.queued += len(p); c.queued > maxQueued {
		c.closed = true
		c.conn.Close()
		return
	}
	c.queue = append(c.queue, outMsg{op, p})
	c.cond.Signal()
}

func (c *client) writer() {
	for {
		c.mu.Lock()
		for len(c.queue) == 0 && !c.closed {
			c.cond.Wait()
		}
		if c.closed && len(c.queue) == 0 {
			c.mu.Unlock()
			return
		}
		batch := c.queue
		c.queue = nil
		c.queued = 0
		c.mu.Unlock()
		for _, m := range batch {
			if c.write(m.op, m.p) != nil {
				c.conn.Close()
				return
			}
		}
	}
}

func (c *client) write(op int, p []byte) error {
	h := []byte{byte(0x80 | op)}
	switch n := len(p); {
	case n < 126:
		h = append(h, byte(n))
	case n < 65536:
		h = append(h, 126, byte(n>>8), byte(n))
	default:
		var b [8]byte
		binary.BigEndian.PutUint64(b[:], uint64(n))
		h = append(append(h, 127), b[:]...)
	}
	_, err := c.conn.Write(append(h, p...))
	return err
}

func (c *client) send(v any) {
	b, _ := json.Marshal(v)
	c.enqueue(1, b)
}

// ---- rooms (all under roomsMu) --------------------------------------------------------------------

func newCode() string {
	const letters = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
	b := make([]byte, 6)
	rand.Read(b)
	for i := range b {
		b[i] = letters[int(b[i])%len(letters)]
	}
	return string(b)
}

func (r *room) members() {
	type member struct {
		Slot    int    `json:"slot"`
		Name    string `json:"name"`
		RTT     int    `json:"rtt"`
		Joining bool   `json:"joining"`
		Pct     int    `json:"pct"`
	}
	list := []member{}
	for k, p := range r.slots {
		if p != nil {
			list = append(list, member{k, p.name, p.rtt, !p.caughtUp, p.pct})
		}
	}
	for _, p := range r.slots {
		if p != nil {
			p.send(map[string]any{"t": "members", "room": r.code, "players": list, "auth": r.auth})
		}
	}
}

// relay logs a game message and queues it for every player but skip (-1: everyone).
func (r *room) relay(m []byte, skip int) {
	r.log = append(r.log, m...)
	for k, p := range r.slots {
		if p != nil && k != skip {
			p.enqueue(2, m)
		}
	}
}

func frameMsg(t byte, slot int, frame int64) []byte {
	m := []byte{t, byte(slot), 0, 0, 0, 0}
	binary.LittleEndian.PutUint32(m[2:], uint32(frame))
	return m
}

// dump queues the room's log for a newcomer, in chunks of whole messages.
func (r *room) dump(c *client) {
	for p := 0; p < len(r.log); {
		q := p
		for q < len(r.log) && q-p < 1800 {
			n := msgLen(r.log[q])
			if n == 0 {
				q = len(r.log)
				break
			}
			q += n
		}
		c.enqueue(2, append([]byte(nil), r.log[p:q]...))
		p = q
	}
}

func (r *room) setAuth() {
	best := -1
	for k, p := range r.slots { // a playing slot if there is one, else anyone
		if p != nil && (best < 0 || (r.from[k] >= 0 && r.from[best] < 0)) {
			best = k
		}
	}
	if best < 0 || best == r.auth {
		return
	}
	r.auth = best
	a := r.slots[best]
	a.enqueue(2, []byte{'R', byte(best)})
	for k, w := range r.pending {
		if w && r.slots[k] != nil {
			a.enqueue(2, []byte{'W', byte(k)})
		}
	}
	log.Printf("room %s: player %d admits newcomers now", r.code, best+1)
}

type request struct {
	T    string `json:"t"`
	Room string `json:"room"`
	Name string `json:"name"`
	Auto bool   `json:"auto"`
	TS   any    `json:"ts"`
	RTT  int    `json:"rtt"`
	Pct  int    `json:"pct"`
}

func cleanName(n string, slot int) string {
	n = strings.TrimSpace(n)
	if len(n) > 16 {
		n = n[:16]
	}
	if n == "" {
		n = "Player " + string(rune('1'+slot))
	}
	return n
}

func (c *client) handle(req request) {
	roomsMu.Lock()
	defer roomsMu.Unlock()
	switch req.T {
	case "host", "join":
		if c.room != nil {
			c.send(map[string]any{"t": "error", "msg": "already in a game"})
			return
		}
		code := strings.ToUpper(strings.TrimSpace(req.Room))
		r := rooms[code]
		creator := false
		if req.T == "host" || (r == nil && req.Auto) {
			if req.T == "host" {
				for code = newCode(); rooms[code] != nil; code = newCode() {
				}
			}
			r = &room{code: code}
			for k := range r.from {
				r.from[k], r.last[k] = -1, -1
			}
			r.from[0] = 0 // the creator plays from the first frame
			rooms[code] = r
			creator = true
			log.Printf("room %s: created", code)
		}
		if r == nil {
			c.send(map[string]any{"t": "error", "msg": "There is no game with that code. Ask your friend for a new link."})
			return
		}
		slot := -1 // the creator is player 1; a newcomer takes the lowest free controller
		for k, p := range r.slots {
			if creator || (p == nil && r.from[k] < 0) {
				slot = k
				break
			}
		}
		if slot < 0 {
			c.send(map[string]any{"t": "error", "msg": "That game is full (4 players)."})
			return
		}
		c.room, c.slot, c.name, c.caughtUp = r, slot, cleanName(req.Name, slot), creator
		r.slots[slot] = c
		c.send(map[string]any{"t": "joined", "room": r.code, "slot": slot, "creator": creator})
		if !creator {
			r.dump(c)
			r.pending[slot] = true
			if a := r.slots[r.auth]; a != nil && r.auth != slot {
				a.enqueue(2, []byte{'W', byte(slot)})
			} else {
				r.setAuth()
			}
			log.Printf("room %s: %s joins as player %d (log %d bytes)", r.code, c.name, slot+1, len(r.log))
		}
		r.members()
	case "ping":
		c.send(map[string]any{"t": "pong", "ts": req.TS})
	case "rtt":
		c.rtt = req.RTT
		if c.room != nil {
			c.room.members()
		}
	case "name":
		if c.room != nil {
			c.name = cleanName(req.Name, c.slot)
			c.room.members()
		}
	case "progress":
		if c.room != nil {
			c.pct, c.caughtUp = req.Pct, req.Pct >= 100
			c.room.members()
		}
	}
}

// game handles one binary message from c (possibly several game messages back to back).
func (c *client) game(b []byte) {
	roomsMu.Lock()
	defer roomsMu.Unlock()
	r := c.room
	if r == nil {
		return
	}
	for len(b) > 0 {
		n := msgLen(b[0])
		if n == 0 || n > len(b) {
			return
		}
		m := append([]byte(nil), b[:n]...)
		b = b[n:]
		k := int(m[1])
		if k >= maxPlayers {
			continue
		}
		switch m[0] {
		case 'I', 'H':
			if k != c.slot {
				continue
			}
			if f := int64(binary.LittleEndian.Uint32(m[2:])); m[0] == 'I' && f > r.last[k] {
				r.last[k] = f
			}
			r.relay(m, c.slot)
		case 'A': // a join grant from the authority: relayed as is (the authority applied it already)
			if c.slot != r.auth {
				continue
			}
			f := int64(binary.LittleEndian.Uint32(m[2:]))
			r.relay(m, c.slot)
			if r.slots[k] == nil { // the newcomer left before the grant: it leaves at once
				r.relay(frameMsg('D', k, f), -1)
				continue
			}
			if r.pending[k] {
				r.pending[k], r.from[k] = false, f
			}
		}
	}
}

func (c *client) leave() {
	roomsMu.Lock()
	defer roomsMu.Unlock()
	r := c.room
	if r == nil || r.slots[c.slot] != c {
		return
	}
	k := c.slot
	r.slots[k] = nil
	r.pending[k] = false
	if r.from[k] >= 0 { // after the leaver's last input
		l := r.last[k] + 1
		if r.from[k] > l {
			l = r.from[k]
		}
		r.relay(frameMsg('D', k, l), -1)
		r.from[k], r.last[k] = -1, -1
	}
	log.Printf("room %s: %s (player %d) left", r.code, c.name, k+1)
	empty := true
	for _, p := range r.slots {
		if p != nil {
			empty = false
		}
	}
	if empty {
		delete(rooms, r.code)
		log.Printf("room %s: closed", r.code)
		return
	}
	if r.auth == k {
		r.auth = -1
		r.setAuth()
	}
	r.members()
}

func serveWS(w http.ResponseWriter, r *http.Request) {
	conn, br, err := upgrade(w, r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	c := &client{conn: conn}
	c.cond = sync.NewCond(&c.mu)
	go c.writer()
	defer func() {
		c.leave()
		c.mu.Lock()
		c.closed = true
		c.cond.Signal()
		c.mu.Unlock()
		conn.Close()
	}()
	for {
		op, msg, err := c.readMessage(br)
		if err != nil {
			return
		}
		if op == 1 {
			var req request
			if json.Unmarshal(msg, &req) == nil {
				c.handle(req)
			}
			continue
		}
		c.game(msg)
	}
}

func main() {
	addr := flag.String("addr", ":8090", "listen address")
	dir := flag.String("dir", "build/web", "web build to serve")
	password := flag.String("password", "", "require this password (HTTP basic auth, any user name)")
	cert := flag.String("cert", "", "TLS certificate file (HTTPS; browsers allow the game's threads only on HTTPS or localhost)")
	key := flag.String("key", "", "TLS private key file")
	flag.Parse()
	files := http.FileServer(http.Dir(*dir))
	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if *password != "" {
			_, p, ok := r.BasicAuth()
			if !ok || subtle.ConstantTimeCompare([]byte(p), []byte(*password)) != 1 {
				w.Header().Set("WWW-Authenticate", `Basic realm="Rugby 08"`)
				http.Error(w, "password required", http.StatusUnauthorized)
				return
			}
		}
		if r.URL.Path == "/ws" {
			serveWS(w, r)
			return
		}
		// SharedArrayBuffer (the game's threads) needs a cross-origin isolated page.
		w.Header().Set("Cross-Origin-Opener-Policy", "same-origin")
		w.Header().Set("Cross-Origin-Embedder-Policy", "require-corp")
		w.Header().Set("Cross-Origin-Resource-Policy", "same-origin")
		files.ServeHTTP(w, r)
	})
	log.Printf("rugby08 server on %s (serving %s, TLS %v)", *addr, *dir, *cert != "")
	if *cert != "" {
		log.Fatal(http.ListenAndServeTLS(*addr, *cert, *key, handler))
	}
	log.Fatal(http.ListenAndServe(*addr, handler))
}
