// UDP listener filter integration test module.
//
// Two filters:
//
//	"test_filter"      — passthrough; returns Continue on every datagram.
//	"stop_iteration"   — drops every datagram by returning StopIteration.
//
// The integration driver sends UDP datagrams through Envoy's udp_proxy and asserts that
// passthrough datagrams reach the upstream while stop_iteration drops them.
package main

import (
	sdk "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go"
	_ "github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/abi"
	"github.com/envoyproxy/envoy/source/extensions/dynamic_modules/sdk/go/shared"
)

func init() {
	sdk.RegisterUdpListenerFilterConfigFactories(map[string]shared.UdpListenerFilterConfigFactory{
		"test_filter":    &passthroughConfigFactory{},
		"stop_iteration": &stopIterationConfigFactory{},
	})
}

func main() {} //nolint:all

// =============================================================================
// passthrough — Continue
// =============================================================================

type passthroughConfigFactory struct {
	shared.EmptyUdpListenerFilterConfigFactory
}

func (passthroughConfigFactory) Create(_ shared.UdpListenerFilterConfigHandle, _ []byte) (shared.UdpListenerFilterFactory, error) {
	return &passthroughFactory{}, nil
}

type passthroughFactory struct {
	shared.EmptyUdpListenerFilterFactory
}

func (*passthroughFactory) Create(_ shared.UdpListenerFilterHandle) shared.UdpListenerFilter {
	return &passthroughFilter{}
}

type passthroughFilter struct {
	shared.EmptyUdpListenerFilter
}

func (*passthroughFilter) OnData(handle shared.UdpListenerFilterHandle) shared.UdpListenerFilterStatus {
	// Exercise a couple of read-only handle accessors so they're hit at runtime — these
	// methods would crash on bad cgo handling rather than return wrong data.
	_ = handle.GetDatagramSize()
	_, _, _ = handle.GetPeerAddress()
	return shared.UdpListenerFilterStatusContinue
}

// =============================================================================
// stop_iteration — StopIteration
// =============================================================================

type stopIterationConfigFactory struct {
	shared.EmptyUdpListenerFilterConfigFactory
}

func (stopIterationConfigFactory) Create(_ shared.UdpListenerFilterConfigHandle, _ []byte) (shared.UdpListenerFilterFactory, error) {
	return &stopIterationFactory{}, nil
}

type stopIterationFactory struct {
	shared.EmptyUdpListenerFilterFactory
}

func (*stopIterationFactory) Create(_ shared.UdpListenerFilterHandle) shared.UdpListenerFilter {
	return &stopIterationFilter{}
}

type stopIterationFilter struct {
	shared.EmptyUdpListenerFilter
}

func (*stopIterationFilter) OnData(_ shared.UdpListenerFilterHandle) shared.UdpListenerFilterStatus {
	return shared.UdpListenerFilterStatusStopIteration
}
