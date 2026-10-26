package main

import (
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterUdpListenerFilterConfigFactories(map[string]shared.UdpListenerFilterConfigFactory{
		"echo_datagram":    &echoDatagramConfigFactory{},
		"rewrite_datagram": &rewriteDatagramConfigFactory{},
	})
}

func main() {}

type echoDatagramConfigFactory struct {
	shared.EmptyUdpListenerFilterConfigFactory
}

func (f *echoDatagramConfigFactory) Create(shared.UdpListenerFilterConfigHandle,
	[]byte) (shared.UdpListenerFilterFactory, error) {
	return &echoDatagramFactory{}, nil
}

type echoDatagramFactory struct {
	shared.EmptyUdpListenerFilterFactory
}

func (f *echoDatagramFactory) Create(handle shared.UdpListenerFilterHandle) shared.UdpListenerFilter {
	return &echoDatagramFilter{handle: handle}
}

type echoDatagramFilter struct {
	handle shared.UdpListenerFilterHandle
	shared.EmptyUdpListenerFilter
}

func (f *echoDatagramFilter) OnData() shared.UdpListenerFilterStatus {
	if f.handle.GetDatagramSize() == 0 {
		panic("expected a non-empty datagram")
	}

	chunks := f.handle.GetDatagramChunks()
	if len(chunks) == 0 {
		panic("expected at least one datagram chunk")
	}
	payload := make([]byte, 0, f.handle.GetDatagramSize())
	for _, chunk := range chunks {
		payload = append(payload, chunk.ToBytes()...)
	}
	if uint64(len(payload)) != f.handle.GetDatagramSize() {
		panic("chunk lengths do not add up to the datagram size")
	}

	peerAddress, peerPort, ok := f.handle.GetPeerAddress()
	peerAddressString := peerAddress.ToString()
	if !ok || peerAddressString == "" || peerPort == 0 {
		panic("expected a peer address")
	}
	if _, _, ok := f.handle.GetLocalAddress(); !ok {
		panic("expected a local address")
	}

	if !f.handle.SendDatagram(payload, peerAddressString, peerPort) {
		panic("failed to send datagram")
	}
	return shared.UdpListenerFilterStatusStop
}

// rewriteDatagram replaces the datagram payload and lets iteration continue, so udp_proxy forwards
// the rewritten bytes upstream.
type rewriteDatagramConfigFactory struct {
	shared.EmptyUdpListenerFilterConfigFactory
}

func (f *rewriteDatagramConfigFactory) Create(handle shared.UdpListenerFilterConfigHandle,
	_ []byte) (shared.UdpListenerFilterFactory, error) {
	return &rewriteDatagramFactory{}, nil
}

type rewriteDatagramFactory struct {
	shared.EmptyUdpListenerFilterFactory
}

func (f *rewriteDatagramFactory) Create(
	handle shared.UdpListenerFilterHandle,
) shared.UdpListenerFilter {
	return &rewriteDatagramFilter{handle: handle}
}

type rewriteDatagramFilter struct {
	handle shared.UdpListenerFilterHandle
	shared.EmptyUdpListenerFilter
}

func (f *rewriteDatagramFilter) OnData() shared.UdpListenerFilterStatus {
	if !f.handle.SetDatagramData([]byte("rewritten")) {
		panic("failed to set datagram data")
	}
	if f.handle.GetDatagramSize() != uint64(len("rewritten")) {
		panic("unexpected datagram size after rewrite")
	}

	return shared.UdpListenerFilterStatusContinue
}
