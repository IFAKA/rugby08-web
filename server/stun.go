package main

// A STUN binding responder (RFC 5389, the part WebRTC's ICE uses): a browser asks for its public
// address (server-reflexive candidate) so peers behind NAT can reach each other directly. UDP, one
// goroutine, no state.

import (
	"encoding/binary"
	"log"
	"net"
)

const stunMagic = 0x2112A442

func serveSTUN(addr string) {
	pc, err := net.ListenPacket("udp", addr)
	if err != nil {
		log.Printf("stun: %v", err)
		return
	}
	log.Printf("stun on %s", addr)
	buf := make([]byte, 1500)
	for {
		n, from, err := pc.ReadFrom(buf)
		if err != nil {
			return
		}
		if resp := stunAnswer(buf[:n], from); resp != nil {
			pc.WriteTo(resp, from)
		}
	}
}

// stunAnswer: a Binding success response with XOR-MAPPED-ADDRESS for a Binding request, else nil.
func stunAnswer(req []byte, from net.Addr) []byte {
	if len(req) < 20 || binary.BigEndian.Uint16(req) != 0x0001 || binary.BigEndian.Uint32(req[4:]) != stunMagic {
		return nil
	}
	ua, ok := from.(*net.UDPAddr)
	if !ok {
		return nil
	}
	ip, family := ua.IP.To4(), byte(1)
	if ip == nil {
		ip, family = ua.IP.To16(), 2
	}
	attr := make([]byte, 4+4+len(ip))
	binary.BigEndian.PutUint16(attr, 0x0020) // XOR-MAPPED-ADDRESS
	binary.BigEndian.PutUint16(attr[2:], uint16(4+len(ip)))
	attr[5] = family
	binary.BigEndian.PutUint16(attr[6:], uint16(ua.Port)^uint16(stunMagic>>16))
	key := append([]byte{}, req[4:20]...) // magic cookie, then the transaction id (IPv6)
	for i := range ip {
		attr[8+i] = ip[i] ^ key[i]
	}
	resp := make([]byte, 20, 20+len(attr))
	binary.BigEndian.PutUint16(resp, 0x0101)
	binary.BigEndian.PutUint16(resp[2:], uint16(len(attr)))
	copy(resp[4:20], req[4:20])
	return append(resp, attr...)
}
