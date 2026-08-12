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

// datagramPayload copies the current datagram out of the Envoy-owned chunks.
func datagramPayload(handle shared.UdpListenerFilterHandle) []byte {
	chunks := handle.GetDatagramChunks()
	if len(chunks) == 0 {
		panic("expected at least one datagram chunk")
	}
	payload := make([]byte, 0, handle.GetDatagramSize())
	for _, chunk := range chunks {
		payload = append(payload, chunk.ToBytes()...)
	}
	if uint64(len(payload)) != handle.GetDatagramSize() {
		panic("chunk lengths do not add up to the datagram size")
	}
	return payload
}

// echoDatagram sends the datagram straight back to its sender and stops iteration, so the
// udp_proxy filter behind it never sees the datagram and the upstream is never reached.
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
	payload := datagramPayload(f.handle)

	peerAddress, peerPort, ok := f.handle.GetPeerAddress()
	if !ok || peerAddress.ToString() == "" || peerPort == 0 {
		panic("expected a peer address")
	}
	if _, _, ok := f.handle.GetLocalAddress(); !ok {
		panic("expected a local address")
	}

	// An empty peer address reuses the current datagram's sender.
	if !f.handle.SendDatagram(payload, "", 0) {
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
	counterID, result := handle.DefineCounter("datagrams_rewritten")
	if result != shared.MetricsSuccess {
		panic("failed to define counter")
	}
	gaugeID, result := handle.DefineGauge("last_datagram_size")
	if result != shared.MetricsSuccess {
		panic("failed to define gauge")
	}
	histogramID, result := handle.DefineHistogram("datagram_size")
	if result != shared.MetricsSuccess {
		panic("failed to define histogram")
	}
	return &rewriteDatagramFactory{
		counterID:   counterID,
		gaugeID:     gaugeID,
		histogramID: histogramID,
	}, nil
}

type rewriteDatagramFactory struct {
	shared.EmptyUdpListenerFilterFactory
	counterID   shared.MetricID
	gaugeID     shared.MetricID
	histogramID shared.MetricID
}

func (f *rewriteDatagramFactory) Create(
	handle shared.UdpListenerFilterHandle,
) shared.UdpListenerFilter {
	return &rewriteDatagramFilter{handle: handle, factory: f}
}

type rewriteDatagramFilter struct {
	handle  shared.UdpListenerFilterHandle
	factory *rewriteDatagramFactory
	shared.EmptyUdpListenerFilter
}

func (f *rewriteDatagramFilter) OnData() shared.UdpListenerFilterStatus {
	size := f.handle.GetDatagramSize()
	// Read the payload before overwriting it so the read path is exercised too.
	_ = datagramPayload(f.handle)

	if !f.handle.SetDatagramData([]byte("rewritten")) {
		panic("failed to set datagram data")
	}
	if f.handle.GetDatagramSize() != uint64(len("rewritten")) {
		panic("unexpected datagram size after rewrite")
	}

	if f.handle.IncrementCounterValue(f.factory.counterID, 1) != shared.MetricsSuccess {
		panic("failed to increment counter")
	}
	if f.handle.SetGaugeValue(f.factory.gaugeID, size) != shared.MetricsSuccess {
		panic("failed to set gauge")
	}
	if f.handle.RecordHistogramValue(f.factory.histogramID, size) != shared.MetricsSuccess {
		panic("failed to record histogram value")
	}

	f.handle.Log(shared.LogLevelInfo, "rewrote datagram on worker %d", f.handle.GetWorkerIndex())
	return shared.UdpListenerFilterStatusContinue
}
