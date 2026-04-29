//go:generate mockgen -source=udp_listener_base.go -destination=mocks/mock_udp_listener_base.go -package=mocks
package shared

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
