package shared

// UdpListenerFilterStatus controls whether Envoy continues UDP listener filter iteration.
type UdpListenerFilterStatus int32

const (
	// UdpListenerFilterStatusContinue lets Envoy continue filter iteration immediately.
	UdpListenerFilterStatusContinue UdpListenerFilterStatus = 0
	// UdpListenerFilterStatusStop stops iteration so later filters never see this datagram.
	UdpListenerFilterStatusStop UdpListenerFilterStatus = 1
	// UdpListenerFilterStatusDefault is the default UDP listener filter result.
	UdpListenerFilterStatusDefault UdpListenerFilterStatus = UdpListenerFilterStatusContinue
)

// UdpListenerFilterHandle exposes the current datagram and the UDP listener's state.
type UdpListenerFilterHandle interface {
	// GetDatagramChunks returns the current datagram payload as Envoy-owned chunks.
	GetDatagramChunks() (chunks []UnsafeEnvoyBuffer)
	// GetDatagramSize returns the total size of the current datagram payload in bytes.
	GetDatagramSize() (size uint64)
	// SetDatagramData replaces the entire payload of the current datagram.
	SetDatagramData(data []byte) (ok bool)
	// GetPeerAddress returns the sender's IP address and port for the current datagram.
	GetPeerAddress() (address UnsafeEnvoyBuffer, port uint32, ok bool)
	// GetLocalAddress returns the local IP address and port the current datagram was received on.
	GetLocalAddress() (address UnsafeEnvoyBuffer, port uint32, ok bool)
	// SendDatagram sends data from the UDP listener socket to peerAddress:peerPort.
	SendDatagram(data []byte, peerAddress string, peerPort uint32) (ok bool)

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
	DefineHistogram(name string) (MetricID, MetricsResult)
	// DefineGauge defines a gauge metric during config creation.
	DefineGauge(name string) (MetricID, MetricsResult)
	// DefineCounter defines a counter metric during config creation.
	DefineCounter(name string) (MetricID, MetricsResult)

	// IncrementCounterValue increases a counter metric by value from the config context.
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
