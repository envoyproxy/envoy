//go:generate mockgen -source=udp_listener.go -destination=mocks/mock_udp_listener.go -package=mocks
package shared

// UDP listener filter SDK surface for dynamic modules.
//
// This mirrors the Rust SDK's `udp_listener` module. UDP listener filters operate on individual
// datagrams as they are received. Unlike TCP listener filters, there is no per-connection state —
// the same filter instance is reused for every datagram on the listener.
//
// A module exposes a UDP listener filter by implementing UdpListenerFilterConfigFactory and
// registering it from an init() function via sdk.RegisterUdpListenerFilterConfigFactories.

// UdpListenerFilterStatus is the status returned by OnData. It corresponds to
// envoy_dynamic_module_type_on_udp_listener_filter_status.
type UdpListenerFilterStatus uint32

const (
	// UdpListenerFilterStatusContinue passes the datagram on to subsequent UDP listener filters.
	UdpListenerFilterStatusContinue UdpListenerFilterStatus = iota
	// UdpListenerFilterStatusStopIteration drops the datagram for any subsequent filters.
	UdpListenerFilterStatusStopIteration
	// UdpListenerFilterStatusDefault equals UdpListenerFilterStatusContinue.
	UdpListenerFilterStatusDefault UdpListenerFilterStatus = UdpListenerFilterStatusContinue
)

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

// UdpListenerFilterConfigHandle is the config-context handle. It is valid for the lifetime of
// the UdpListenerFilterFactory and is used to define metrics scoped to the filter configuration.
type UdpListenerFilterConfigHandle interface {
	// DefineCounter creates a per-config counter.
	DefineCounter(name string) (MetricID, MetricsResult)

	// DefineGauge creates a per-config gauge.
	DefineGauge(name string) (MetricID, MetricsResult)

	// DefineHistogram creates a per-config histogram.
	DefineHistogram(name string) (MetricID, MetricsResult)
}

// UdpListenerFilterHandle is the handle used inside OnData to interact with the current
// datagram.
type UdpListenerFilterHandle interface {
	// GetDatagramData returns the current datagram data as a slice of chunks. Valid only
	// inside the OnData callback.
	//
	// NOTE: The buffers are owned by Envoy and only valid for the duration of the callback.
	// Copy if you need to keep them.
	GetDatagramData() []UnsafeEnvoyBuffer

	// GetDatagramSize returns the total byte size of the current datagram.
	GetDatagramSize() uint64

	// SetDatagramData replaces the current datagram contents with the given bytes. Subsequent
	// listener filters and the eventual UDP session will see the modified datagram. Returns
	// true on success.
	SetDatagramData(data []byte) bool

	// GetPeerAddress returns the remote (sender) address and port. Returns false if the
	// address is not available.
	GetPeerAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// GetLocalAddress returns the local address and port for the listener.
	GetLocalAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// SendDatagram sends data as a UDP datagram to the given peer. Returns true on success.
	SendDatagram(data []byte, peerAddress string, peerPort uint32) bool

	// ---- metrics ----

	IncrementCounter(id MetricID, value uint64) MetricsResult
	SetGauge(id MetricID, value uint64) MetricsResult
	IncrementGauge(id MetricID, value uint64) MetricsResult
	DecrementGauge(id MetricID, value uint64) MetricsResult
	RecordHistogramValue(id MetricID, value uint64) MetricsResult

	// GetWorkerIndex returns the worker thread index assigned to this filter.
	GetWorkerIndex() uint32
}
