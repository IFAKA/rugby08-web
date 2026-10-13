package main

import (
	"encoding/binary"
	"net"
	"testing"
)

func TestStunAnswer(t *testing.T) {
	req := make([]byte, 20)
	binary.BigEndian.PutUint16(req, 0x0001)
	binary.BigEndian.PutUint32(req[4:], stunMagic)
	copy(req[8:], "transaction!")
	resp := stunAnswer(req, &net.UDPAddr{IP: net.IPv4(203, 0, 113, 7), Port: 54321})
	if resp == nil || binary.BigEndian.Uint16(resp) != 0x0101 || string(resp[8:20]) != "transaction!" {
		t.Fatalf("not a binding success for the request: %x", resp)
	}
	a := resp[20:]
	if binary.BigEndian.Uint16(a) != 0x0020 || a[5] != 1 {
		t.Fatalf("no IPv4 XOR-MAPPED-ADDRESS: %x", a)
	}
	port := binary.BigEndian.Uint16(a[6:]) ^ uint16(stunMagic>>16)
	ip := net.IPv4(a[8]^0x21, a[9]^0x12, a[10]^0xA4, a[11]^0x42)
	if port != 54321 || !ip.Equal(net.IPv4(203, 0, 113, 7)) {
		t.Fatalf("mapped %v:%d", ip, port)
	}
	if stunAnswer(req[:10], &net.UDPAddr{}) != nil {
		t.Fatal("answered a short packet")
	}
}
