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
//	<- {"t":"joined","room":C,"slot":S,"creator":bool,"since":F}  then the room's log, then live messages
//	   (since: where the slot's previous player left, -1 none: the log's 'A'/'D' for S before it are that player's)
//	<- {"t":"members","room":C,"players":[{slot,name,rtt,joining,pct}]}
//	-> {"t":"ping","ts":X}  <- {"t":"pong","ts":X}    -> {"t":"rtt","rtt":MS}
//	-> {"t":"name","name":N}                          -> {"t":"progress","pct":P}  (catching up)
//	<- {"t":"error","msg":M}
//	-> {"t":"peek","room":C}  <- {"t":"peek","ok":bool[,"msg":M][,"slot":S,"players":MASK]}  (can C be joined?)
//	-> {"t":"stall","slot":S}  a player's input stopped coming: the server drops it
//
// Game messages (binary, netplay.c): 'I' inputs and 'H' state hashes from each player and 'A' join
// grants from the room's authority are logged and relayed in one order to everyone else; the server
// adds 'D' when a player leaves (after that player's last input), sends 'W' (a newcomer waits) to the
// authority and 'R' to a player that becomes the authority when the previous one leaves.
//
// Host-mode messages (on.match_net: one game, the host's, plays the match; friends show it), framed
// {u8 type, u8 slot, u32 frame, u16 len, len bytes} and relayed to everyone else but never logged:
// 'S' the match setup (host, at launch; the room keeps the latest), 'F' a match frame (host; payload
// byte 0 = 0 is a key frame, the room keeps the latest), 'E' the match is over (host; the room forgets
// setup and key), 'U' paused or not (host), 'K' a key request, 'P' a pad and 'Z' a pause request
// (friends), 'M' the host's front-end screen (u16 id; the room keeps the latest), 'T' its Team Select
// (relayed on change and every second), 'C' a match event the host's cameras heard (relayed), 'Y' a
// friend's resume. When a party's host leaves, the room closes: {"t":"ended"} to everyone left. A newcomer gets the
// room's log, then the kept screen, setup and key frame.
package main

import (
	"bufio"
	"crypto/rand"
	"crypto/sha1"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"errors"
	"flag"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"
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
	build   string // the creator's game build: lockstep needs the same game everywhere
	slots   [maxPlayers]*client
	auth    int               // slot that grants joins
	log     []byte            // every game message since frame 0, in relay order
	from    [maxPlayers]int64 // slot plays from this frame (-1: not granted)
	last    [maxPlayers]int64 // last input frame received from the slot
	pending [maxPlayers]bool  // waiting for a grant
	freed   [maxPlayers]int64 // the slot's last player left at this frame: its 'A'/'D' before it are not a newcomer's (-1)
	granted [maxPlayers]int64 // the slot's latest grant frame (-1)
	forks   []forkReq         // players leaving into a room of their own, waiting for inputs (fork.go)
	setup   []byte            // host mode: the latest 'S' message, for newcomers
	key     []byte            // host mode: the latest key 'F' message, for newcomers
	menu    []byte            // host mode: the latest 'M' (the host's front-end screen), for newcomers
}

var (
	roomsMu sync.Mutex // rooms and everything in them; held while queueing, so all players see one order
	rooms   = map[string]*room{}
)

// msgLen: the length of the message at the start of b (0: unknown type; more than len(b): incomplete).
// 'I' is 19 bytes plus 6 per keyboard/mouse event (its byte 18 counts them).
func msgLen(b []byte) int {
	switch b[0] {
	case 'I':
		if len(b) < 19 {
			return 19
		}
		return 19 + 6*int(b[18])
	case 'H':
		return 14
	case 'A', 'D':
		return 6
	case 'W', 'R':
		return 2
	case 'X': // external data (runtime/core/net_external.c): type, slot, u32 frame, u32 seq, u32 len, data
		if len(b) < 14 {
			return 14
		}
		return 14 + int(binary.LittleEndian.Uint32(b[10:]))
	case 'S', 'F', 'K', 'P', 'E', 'Z', 'U', 'M', 'T', 'C', 'Y':
		if len(b) < haHeader {
			return haHeader
		}
		return haHeader + int(binary.LittleEndian.Uint16(b[6:]))
	}
	return 0
}

// haHeader: the header of the host-mode messages ('S', 'F', 'K', 'P', 'E', 'Z', 'U'): type, slot, u32 frame,
// u16 len.
const haHeader = 8

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

// pass queues a host-mode message for every player but skip, without logging it.
func (r *room) pass(m []byte, skip int) {
	for k, p := range r.slots {
		if p != nil && k != skip {
			p.enqueue(2, m)
		}
	}
}

// dumpHA queues the room's kept setup and key frame for a newcomer (after its log).
func (r *room) dumpHA(c *client) {
	for _, m := range [][]byte{r.menu, r.setup, r.key} {
		if m != nil {
			c.enqueue(2, m)
		}
	}
}

// dump queues the room's log for a newcomer, in chunks of whole messages.
func (r *room) dump(c *client) {
	for p := 0; p < len(r.log); {
		q := p
		for q < len(r.log) && q-p < 1800 {
			n := msgLen(r.log[q:])
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

// a page of another game build cannot play in lockstep with the room (both reload the page to update)
const anotherBuild = "That party runs another version of the game: reload both pages"

type request struct {
	T     string          `json:"t"`
	Room  string          `json:"room"`
	Name  string          `json:"name"`
	Auto  bool            `json:"auto"`
	Build string          `json:"build"`
	TS    any             `json:"ts"`
	RTT   int             `json:"rtt"`
	Pct   int             `json:"pct"`
	Slot  int             `json:"slot"`
	Frame int64           `json:"frame"`
	To    *int            `json:"to"`
	Data  json.RawMessage `json:"data"`
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
			r = &room{code: code, build: req.Build}
			for k := range r.from {
				r.from[k], r.last[k], r.freed[k], r.granted[k] = -1, -1, -1, -1
			}
			r.from[0] = 0 // the creator plays from the first frame
			rooms[code] = r
			creator = true
			log.Printf("room %s: created", code)
		}
		if r == nil {
			c.send(map[string]any{"t": "error", "msg": "No party with that code"})
			return
		}
		if r.build != req.Build {
			c.send(map[string]any{"t": "error", "msg": anotherBuild})
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
			c.send(map[string]any{"t": "error", "msg": "Party full"})
			return
		}
		c.room, c.slot, c.name, c.caughtUp = r, slot, cleanName(req.Name, slot), creator
		r.slots[slot] = c
		c.send(map[string]any{"t": "joined", "room": r.code, "slot": slot, "creator": creator, "stun": stunPort,
			"since": r.freed[slot]})
		if !creator {
			r.dump(c)
			r.dumpHA(c)
			r.pending[slot] = true
			if a := r.slots[r.auth]; a != nil && r.auth != slot {
				a.enqueue(2, []byte{'W', byte(slot)})
			} else {
				r.setAuth()
			}
			log.Printf("room %s: %s joins as player %d (log %d bytes)", r.code, c.name, slot+1, len(r.log))
		}
		r.members()
	case "peek": // JOIN PARTY asks first: can that room be joined? (the page reloads into it only then)
		r := rooms[strings.ToUpper(strings.TrimSpace(req.Room))]
		free, slot, players := false, -1, 0 // the slot a join takes now (as "join" picks it), who plays there
		for k := 0; r != nil && k < maxPlayers; k++ {
			open := r.slots[k] == nil && r.from[k] < 0
			if open && slot < 0 {
				slot = k
			}
			if !open {
				players |= 1 << k
			}
			free = free || open
		}
		switch {
		case r == nil:
			c.send(map[string]any{"t": "peek", "ok": false, "msg": "No party with that code"})
		case r.build != req.Build:
			c.send(map[string]any{"t": "peek", "ok": false, "msg": anotherBuild})
		case !free:
			c.send(map[string]any{"t": "peek", "ok": false, "msg": "Party full"})
		default:
			c.send(map[string]any{"t": "peek", "ok": true, "slot": slot, "players": players})
		}
	case "stall": // a player's input stopped coming: drop it so nobody waits (its game is told it left)
		r := c.room
		if r == nil || req.Slot < 0 || req.Slot >= maxPlayers || req.Slot == c.slot {
			return
		}
		if p := r.slots[req.Slot]; p != nil && !r.pending[req.Slot] {
			log.Printf("room %s: player %d stalled (reported by player %d): dropped", r.code, req.Slot+1, c.slot+1)
			p.send(map[string]any{"t": "error", "msg": "Your connection was too slow: you left the game"})
			p.conn.Close()
		}
	case "fork": // LEAVE PARTY in lockstep: this game plays on in a room of its own (fork.go)
		if r := c.room; r != nil && req.Frame > 0 {
			r.forks = append(r.forks, forkReq{c, req.Frame})
			r.tryForks()
		}
	case "signal":
		c.signal(req)
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
		n := msgLen(b)
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
		case 'X': // logged like the inputs: a game catching up later needs it at the same point
			if k == c.slot {
				r.relay(m, c.slot)
			}
		case 'I', 'H':
			if k != c.slot {
				continue
			}
			if f := int64(binary.LittleEndian.Uint32(m[2:])); m[0] == 'I' && f > r.last[k] {
				r.last[k] = f
			}
			r.relay(m, c.slot)
			if len(r.forks) > 0 { // (a leaver waiting for this input: fork.go)
				r.tryForks()
			}
		case 'A': // a join grant from the authority: relayed as is (the authority applied it already)
			if c.slot != r.auth {
				continue
			}
			f := int64(binary.LittleEndian.Uint32(m[2:]))
			r.relay(m, c.slot)
			if f > r.granted[k] {
				r.granted[k] = f
			}
			if r.slots[k] == nil { // the newcomer left before the grant: it leaves at once
				r.relay(frameMsg('D', k, f), -1)
				r.freed[k] = f + 1
				continue
			}
			if r.pending[k] {
				r.pending[k], r.from[k] = false, f
			}
			if p := r.slots[k]; p != nil && !p.caughtUp { // told at once, ahead of the log it replays
				p.send(map[string]any{"t": "granted", "from": f})
			}
		case 'S', 'F', 'K', 'P', 'E', 'Z', 'U', 'M', 'T', 'C', 'Y':
			if k != c.slot {
				continue
			}
			switch {
			case m[0] == 'S':
				r.setup = m
			case m[0] == 'M':
				r.menu = m
			case m[0] == 'F' && len(m) > haHeader && m[haHeader] == 0:
				r.key = m
			case m[0] == 'E': // the match is over: a newcomer must not be sent into it
				r.setup, r.key = nil, nil
			}
			r.pass(m, c.slot)
		}
	}
}

func (c *client) leave() {
	roomsMu.Lock()
	defer roomsMu.Unlock()
	if r := c.room; r != nil && r.slots[c.slot] == c {
		r.drop(c)
	}
}

// drop: c leaves its room r (the others get its 'D'); the room closes when empty.
func (r *room) drop(c *client) {
	k := c.slot
	r.slots[k] = nil
	r.pending[k] = false
	if r.from[k] >= 0 { // after the leaver's last input
		l := r.last[k] + 1
		if r.from[k] > l {
			l = r.from[k]
		}
		r.relay(frameMsg('D', k, l), -1)
		r.from[k], r.last[k], r.freed[k] = -1, -1, l
	}
	log.Printf("room %s: %s (player %d) left", r.code, c.name, k+1)
	if k == 0 && (r.setup != nil || r.menu != nil) { // a party's host: the party is over
		for _, p := range r.slots {
			if p != nil {
				p.send(map[string]any{"t": "ended", "room": r.code})
				p.room = nil
			}
		}
		delete(rooms, r.code)
		log.Printf("room %s: closed (its host left)", r.code)
		return
	}
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
	r.tryForks()
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
	password := flag.String("password", "", "private site: only the party link https://host/k/PASSWORD lets in (gate.go); empty: public")
	cert := flag.String("cert", "", "TLS certificate file (HTTPS; browsers allow the game's threads only on HTTPS or localhost)")
	key := flag.String("key", "", "TLS private key file")
	stun := flag.Int("stun", 0, "answer STUN on this UDP port (WebRTC peers behind NAT find their public address)")
	go frontHints()
	logDir := flag.String("logdir", "", "where the players' game logs go (default: DIR/../../logs/play)")
	flag.Parse()
	playLogDir = *logDir
	if playLogDir == "" {
		playLogDir = filepath.Join(*dir, "..", "..", "logs", "play")
	}
	if err := os.MkdirAll(playLogDir, 0o755); err != nil {
		log.Printf("play logs off: %v", err)
		playLogDir = ""
	}
	if *stun > 0 {
		stunPort = *stun
		go serveSTUN(net.JoinHostPort("", strconv.Itoa(*stun)))
	}
	files := http.FileServer(http.Dir(*dir))
	g := gate{password: *password}
	handler := http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !g.allow(w, r) { // the private party site (gate.go)
			return
		}
		if r.URL.Path == "/log" { // the players' game logs (playlog.go)
			servePlayLog(w, r)
			return
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

// frontHints: every 200 ms each game still catching up hears how far the others' inputs go ("front"), so
// it can send idle inputs that far (runtime/core/netplay.c hint) and nobody waits for it.
func frontHints() {
	for range time.Tick(200 * time.Millisecond) {
		roomsMu.Lock()
		for _, r := range rooms {
			front := int64(-1)
			for k, p := range r.slots {
				if p != nil && p.caughtUp && r.last[k] > front {
					front = r.last[k]
				}
			}
			for _, p := range r.slots {
				if p != nil && !p.caughtUp && front >= 0 {
					p.send(map[string]any{"t": "front", "frame": front})
				}
			}
		}
		roomsMu.Unlock()
	}
}
