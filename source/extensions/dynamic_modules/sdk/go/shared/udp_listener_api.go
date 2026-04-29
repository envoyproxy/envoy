//go:generate mockgen -source=udp_listener_api.go -destination=mocks/mock_udp_listener_api.go -package=mocks
package shared

// UDP listener filter SDK surface for dynamic modules.
//
// This mirrors the Rust SDK's `udp_listener` module. UDP listener filters operate on individual
// datagrams as they are received. Unlike TCP listener filters, there is no per-connection state —
// the same filter instance is reused for every datagram on the listener.
//
// A module exposes a UDP listener filter by implementing UdpListenerFilterConfigFactory and
// registering it from an init() function via sdk.RegisterUdpListenerFilterConfigFactories.

// UdpListenerFilter is the module-side UDP listener filter object. A single instance is created
// per UDP listener configuration and is invoked for every datagram on that listener. Unlike TCP
// filters, there is no per-connection lifecycle.
//
// Embed EmptyUdpListenerFilter to opt out of OnData.
type UdpListenerFilter interface {
	// OnData is called when a UDP datagram is received. Use the handle to read the datagram,
	// inspect the peer address, send a reply, or record metrics.
	OnData(handle UdpListenerFilterHandle) UdpListenerFilterStatus

	// OnDestroy is called when the filter is being destroyed. No handle is passed because the
	// underlying Envoy filter pointer is no longer valid.
	OnDestroy()
}

// EmptyUdpListenerFilter is a no-op UdpListenerFilter that returns Continue/Default for OnData.
type EmptyUdpListenerFilter struct{}

func (*EmptyUdpListenerFilter) OnData(_ UdpListenerFilterHandle) UdpListenerFilterStatus {
	return UdpListenerFilterStatusDefault
}
func (*EmptyUdpListenerFilter) OnDestroy() {}

// UdpListenerFilterFactory creates the per-listener UdpListenerFilter instance.
type UdpListenerFilterFactory interface {
	// Create creates the UdpListenerFilter for the listener.
	Create(handle UdpListenerFilterHandle) UdpListenerFilter

	// OnDestroy is called when the factory is destroyed.
	OnDestroy()
}

// EmptyUdpListenerFilterFactory is a no-op UdpListenerFilterFactory.
type EmptyUdpListenerFilterFactory struct{}

func (*EmptyUdpListenerFilterFactory) Create(_ UdpListenerFilterHandle) UdpListenerFilter {
	return &EmptyUdpListenerFilter{}
}
func (*EmptyUdpListenerFilterFactory) OnDestroy() {}

// UdpListenerFilterConfigFactory is the top-level factory the module registers via
// sdk.RegisterUdpListenerFilterConfigFactories.
type UdpListenerFilterConfigFactory interface {
	// Create parses unparsedConfig and returns a factory.
	Create(handle UdpListenerFilterConfigHandle, unparsedConfig []byte) (UdpListenerFilterFactory, error)
}

// EmptyUdpListenerFilterConfigFactory is a no-op UdpListenerFilterConfigFactory.
type EmptyUdpListenerFilterConfigFactory struct{}

func (*EmptyUdpListenerFilterConfigFactory) Create(_ UdpListenerFilterConfigHandle, _ []byte) (UdpListenerFilterFactory, error) {
	return &EmptyUdpListenerFilterFactory{}, nil
}
