package shared

// UdpListenerFilterStatus controls whether Envoy continues UDP listener filter iteration.
type UdpListenerFilterStatus int32

const (
	// UdpListenerFilterStatusContinue lets Envoy pass the datagram to the next UDP listener filter.
	UdpListenerFilterStatusContinue UdpListenerFilterStatus = 0
	// UdpListenerFilterStatusStop stops iteration so later filters never see this datagram.
	//
	// Unlike the TCP listener filter chain, there is no callback to resume iteration afterwards:
	// the decision applies to the current datagram only, and the next datagram starts a fresh
	// iteration.
	UdpListenerFilterStatusStop UdpListenerFilterStatus = 1
	// UdpListenerFilterStatusDefault is the default UDP listener filter result.
	UdpListenerFilterStatusDefault UdpListenerFilterStatus = UdpListenerFilterStatusContinue
)

// UdpListenerFilterHandle exposes the current datagram and the UDP listener's state.
//
// The datagram accessors are only valid for the duration of an UdpListenerFilter.OnData call.
// Outside of it Envoy holds no current datagram, so they report no data.
type UdpListenerFilterHandle interface {
	// GetDatagramChunks returns the current datagram payload as Envoy-owned chunks.
	//
	// The chunks alias Envoy memory and are only valid for the duration of the OnData call; copy
	// the bytes if you need to retain them. Returns nil outside of OnData or for an empty datagram.
	GetDatagramChunks() []UnsafeEnvoyBuffer
	// GetDatagramSize returns the total size of the current datagram payload in bytes.
	//
	// Returns 0 outside of OnData.
	GetDatagramSize() uint64
	// SetDatagramData replaces the entire payload of the current datagram.
	//
	// Passing empty data clears the payload. The provided bytes are owned by the caller for the
	// duration of the call. Returns false outside of OnData.
	SetDatagramData(data []byte) bool

	// GetPeerAddress returns the sender's IP address and port for the current datagram.
	//
	// Returns false outside of OnData, or when the peer address is not an IP address.
	GetPeerAddress() (UnsafeEnvoyBuffer, uint32, bool)
	// GetLocalAddress returns the local IP address and port the current datagram was received on.
	//
	// Returns false outside of OnData, or when the local address is not an IP address.
	GetLocalAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// SendDatagram sends data from the UDP listener socket to peerAddress:peerPort.
	//
	// An empty peerAddress reuses the current datagram's sender, which makes this an echo back to
	// the client; that form only works during OnData. peerAddress must be an IP address literal,
	// not a hostname. The provided bytes are owned by the caller for the duration of the call.
	//
	// It returns false if the address cannot be parsed or the listener has no local address to send
	// from.
	SendDatagram(data []byte, peerAddress string, peerPort uint32) bool

	// IncrementCounterValue increases a counter metric by value.
	IncrementCounterValue(id MetricID, value uint64) MetricsResult
	// SetGaugeValue sets a gauge metric to value.
	SetGaugeValue(id MetricID, value uint64) MetricsResult
	// IncrementGaugeValue increases a gauge metric by value.
	IncrementGaugeValue(id MetricID, value uint64) MetricsResult
	// DecrementGaugeValue decreases a gauge metric by value.
	DecrementGaugeValue(id MetricID, value uint64) MetricsResult
	// RecordHistogramValue records value in a histogram metric.
	RecordHistogramValue(id MetricID, value uint64) MetricsResult

	// GetWorkerIndex returns the Envoy worker index this filter instance belongs to.
	GetWorkerIndex() uint32

	// Log writes a formatted message through Envoy's logging subsystem.
	Log(level LogLevel, format string, args ...any)
}

// UdpListenerFilterConfigHandle exposes host services during UDP listener filter config creation.
type UdpListenerFilterConfigHandle interface {
	// DefineHistogram defines a histogram metric during config creation.
	//
	// Metrics can only be defined while the configuration is being created; afterwards Envoy
	// freezes metric creation and this returns MetricsFrozen.
	DefineHistogram(name string) (MetricID, MetricsResult)
	// DefineGauge defines a gauge metric during config creation.
	DefineGauge(name string) (MetricID, MetricsResult)
	// DefineCounter defines a counter metric during config creation.
	DefineCounter(name string) (MetricID, MetricsResult)

	// IncrementCounterValue increases a counter metric by value from the config context.
	//
	// Unlike UdpListenerFilterHandle.IncrementCounterValue, this does not require a per-worker
	// filter and can be called outside of datagram processing.
	IncrementCounterValue(id MetricID, value uint64) MetricsResult
	// SetGaugeValue sets a gauge metric to value from the config context.
	SetGaugeValue(id MetricID, value uint64) MetricsResult
	// IncrementGaugeValue increases a gauge metric by value from the config context.
	IncrementGaugeValue(id MetricID, value uint64) MetricsResult
	// DecrementGaugeValue decreases a gauge metric by value from the config context.
	DecrementGaugeValue(id MetricID, value uint64) MetricsResult
	// RecordHistogramValue records value into a histogram metric from the config context.
	RecordHistogramValue(id MetricID, value uint64) MetricsResult

	// Log writes a formatted message through Envoy's logging subsystem.
	Log(level LogLevel, format string, args ...any)
}
