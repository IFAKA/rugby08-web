// Rugby 08 online relay: serves the web build and relays lockstep inputs between the players of a room.
// Standard library only (WebSocket implemented here). One process, no external services.
//
//	go run ./server -addr :8090 -dir build/web [-password SECRET]
//
// Lobby protocol (JSON text messages):
//
//	-> {"t":"host","name":N,"players":P}            create a room with a fresh code (you are player 1)
//	-> {"t":"join","room":C,"name":N[,"players":P,"delay":D,"auto":true]}  join (creates C if missing)
//	<- {"t":"joined","room":C,"slot":S,"host":bool}
//	<- {"t":"lobby","room":C,"players":[names],"need":P}
//	-> {"t":"start","delay":D}                       host only
//	<- {"t":"start","players":P,"delay":D}
//	<- {"t":"left","slot":S}                          a player disconnected (the session ends)
//	-> {"t":"ping","ts":X}  <- {"t":"pong","ts":X}
//	<- {"t":"error","msg":M}
//
// Binary messages (game inputs and state hashes) are relayed unchanged to the other players.
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

type client struct {
	conn net.Conn
	mu   sync.Mutex // writes
	room *room
	slot int
	name string
}

type room struct {
	code    string
	players []*client
	need    int
	delay   int
	auto    bool
	started bool
}

var (
	roomsMu sync.Mutex
	rooms   = map[string]*room{}
)

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
			c.write(10, p)
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
	c.mu.Lock()
	defer c.mu.Unlock()
	if _, err := c.conn.Write(h); err != nil {
		return err
	}
	_, err := c.conn.Write(p)
	return err
}

func (c *client) send(v any) {
	b, _ := json.Marshal(v)
	c.write(1, b)
}

// ---- rooms --------------------------------------------------------------------------------------

func newCode() string {
	const letters = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
	b := make([]byte, 6)
	rand.Read(b)
	for i := range b {
		b[i] = letters[int(b[i])%len(letters)]
	}
	return string(b)
}

func (r *room) lobby() {
	names := []string{}
	for _, p := range r.players {
		names = append(names, p.name)
	}
	for _, p := range r.players {
		p.send(map[string]any{"t": "lobby", "room": r.code, "players": names, "need": r.need})
	}
}

func (r *room) start() {
	r.started = true
	for _, p := range r.players {
		p.send(map[string]any{"t": "start", "players": len(r.players), "delay": r.delay})
	}
	log.Printf("room %s: started with %d players, delay %d", r.code, len(r.players), r.delay)
}

type request struct {
	T       string `json:"t"`
	Room    string `json:"room"`
	Name    string `json:"name"`
	Players int    `json:"players"`
	Delay   int    `json:"delay"`
	Auto    bool   `json:"auto"`
	TS      any    `json:"ts"`
}

func (c *client) handle(req request) {
	roomsMu.Lock()
	defer roomsMu.Unlock()
	switch req.T {
	case "host", "join":
		if c.room != nil {
			c.send(map[string]any{"t": "error", "msg": "already in a room"})
			return
		}
		code := strings.ToUpper(strings.TrimSpace(req.Room))
		if req.T == "host" {
			for code = newCode(); rooms[code] != nil; code = newCode() {
			}
		}
		r := rooms[code]
		if r == nil {
			if req.T == "join" && !req.Auto {
				c.send(map[string]any{"t": "error", "msg": "no game with that code"})
				return
			}
			need := req.Players
			if need < 1 || need > 4 {
				need = 2
			}
			delay := req.Delay
			if delay < 1 {
				delay = 4
			}
			r = &room{code: code, need: need, delay: delay, auto: req.Auto}
			rooms[code] = r
		}
		if r.started || len(r.players) >= 4 {
			c.send(map[string]any{"t": "error", "msg": "that game is full or already playing"})
			return
		}
		c.room, c.slot, c.name = r, len(r.players), req.Name
		if c.name == "" {
			c.name = "Player " + string(rune('1'+c.slot))
		}
		r.players = append(r.players, c)
		c.send(map[string]any{"t": "joined", "room": r.code, "slot": c.slot, "host": c.slot == 0})
		r.lobby()
		if r.auto && len(r.players) == r.need {
			r.start()
		}
	case "start":
		if r := c.room; r != nil && c.slot == 0 && !r.started {
			if req.Delay > 0 {
				r.delay = req.Delay
			}
			r.start()
		}
	case "ping":
		c.send(map[string]any{"t": "pong", "ts": req.TS})
	}
}

func (c *client) leave() {
	roomsMu.Lock()
	defer roomsMu.Unlock()
	r := c.room
	if r == nil {
		return
	}
	for i, p := range r.players {
		if p == c {
			r.players = append(r.players[:i], r.players[i+1:]...)
			break
		}
	}
	if r.started {
		for _, p := range r.players {
			p.send(map[string]any{"t": "left", "slot": c.slot})
		}
	} else {
		for i, p := range r.players { // renumber the lobby
			p.slot = i
			p.send(map[string]any{"t": "joined", "room": r.code, "slot": i, "host": i == 0})
		}
		r.lobby()
	}
	if len(r.players) == 0 {
		delete(rooms, r.code)
	}
}

func serveWS(w http.ResponseWriter, r *http.Request) {
	conn, br, err := upgrade(w, r)
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	c := &client{conn: conn}
	defer func() { c.leave(); conn.Close() }()
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
		roomsMu.Lock() // binary: relay to the other players
		var peers []*client
		if c.room != nil && c.room.started {
			for _, p := range c.room.players {
				if p != c {
					peers = append(peers, p)
				}
			}
		}
		roomsMu.Unlock()
		for _, p := range peers {
			p.write(2, msg)
		}
	}
}

func main() {
	addr := flag.String("addr", ":8090", "listen address")
	dir := flag.String("dir", "build/web", "web build to serve")
	password := flag.String("password", "", "require this password (HTTP basic auth, any user name)")
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
	log.Printf("rugby08 server on %s (serving %s)", *addr, *dir)
	log.Fatal(http.ListenAndServe(*addr, handler))
}
