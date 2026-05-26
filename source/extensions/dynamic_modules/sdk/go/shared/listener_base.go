package shared

// ListenerFilterStatus controls whether Envoy continues listener filter iteration.
type ListenerFilterStatus int32

const (
	// ListenerFilterStatusContinue lets Envoy continue listener filter iteration immediately.
	ListenerFilterStatusContinue ListenerFilterStatus = 0
	// ListenerFilterStatusStop pauses listener filter iteration until ContinueFilterChain is called.
	ListenerFilterStatusStop ListenerFilterStatus = 1
	// ListenerFilterStatusDefault is the default listener filter result.
	ListenerFilterStatusDefault ListenerFilterStatus = ListenerFilterStatusContinue
)

// ListenerAddressType describes which local address flavor Envoy is currently using.
type ListenerAddressType uint32

const (
	// ListenerAddressTypeUnknown means Envoy could not classify the address type.
	ListenerAddressTypeUnknown ListenerAddressType = iota
	// ListenerAddressTypeIP reports an IP socket address.
	ListenerAddressTypeIP
	// ListenerAddressTypePipe reports a pipe or Unix-domain socket address.
	ListenerAddressTypePipe
	// ListenerAddressTypeEnvoyInternal reports an Envoy internal address.
	ListenerAddressTypeEnvoyInternal
)

// ListenerFilterHandle exposes the accepted socket and listener-filter state.
type ListenerFilterHandle interface {
	// GetBufferChunk returns the current peek buffer chunk for this listener filter.
	GetBufferChunk() (UnsafeEnvoyBuffer, bool)
	// DrainBuffer drains length bytes from the listener filter peek buffer.
	DrainBuffer(length uint64) bool

	// SetDetectedTransportProtocol overrides the detected downstream transport protocol.
	SetDetectedTransportProtocol(protocol string)
	// SetRequestedServerName overrides the downstream requested server name.
	SetRequestedServerName(name string)
	// SetRequestedApplicationProtocols overrides the downstream requested ALPN protocols.
	SetRequestedApplicationProtocols(protocols []string)
	// SetJA3Hash overrides the JA3 TLS fingerprint.
	SetJA3Hash(hash string)
	// SetJA4Hash overrides the JA4 TLS fingerprint.
	SetJA4Hash(hash string)

	// GetRequestedServerName returns the requested server name (SNI), if present.
	GetRequestedServerName() (UnsafeEnvoyBuffer, bool)
	// GetDetectedTransportProtocol returns the detected downstream transport protocol, if present.
	GetDetectedTransportProtocol() (UnsafeEnvoyBuffer, bool)
	// GetRequestedApplicationProtocols returns the requested ALPN protocol list.
	GetRequestedApplicationProtocols() []UnsafeEnvoyBuffer
	// GetJA3Hash returns the JA3 TLS fingerprint, if present.
	GetJA3Hash() (UnsafeEnvoyBuffer, bool)
	// GetJA4Hash returns the JA4 TLS fingerprint, if present.
	GetJA4Hash() (UnsafeEnvoyBuffer, bool)
	// IsSSL reports whether the downstream connection is using TLS.
	IsSSL() bool
	// GetSSLURISANs returns the peer certificate URI SAN entries.
	GetSSLURISANs() []UnsafeEnvoyBuffer
	// GetSSLDNSSANs returns the peer certificate DNS SAN entries.
	GetSSLDNSSANs() []UnsafeEnvoyBuffer
	// GetSSLSubject returns the peer certificate subject, if present.
	GetSSLSubject() (UnsafeEnvoyBuffer, bool)

	// GetRemoteAddress returns the effective remote address and port.
	GetRemoteAddress() (UnsafeEnvoyBuffer, uint32, bool)
	// GetDirectRemoteAddress returns the direct remote address and port before proxy handling.
	GetDirectRemoteAddress() (UnsafeEnvoyBuffer, uint32, bool)
	// GetLocalAddress returns the effective local address and port.
	GetLocalAddress() (UnsafeEnvoyBuffer, uint32, bool)
	// GetDirectLocalAddress returns the original local address and port before restoration.
	GetDirectLocalAddress() (UnsafeEnvoyBuffer, uint32, bool)
	// GetOriginalDst returns the original destination address and port, if present.
	GetOriginalDst() (UnsafeEnvoyBuffer, uint32, bool)
	// GetAddressType returns the current local address type.
	GetAddressType() ListenerAddressType
	// IsLocalAddressRestored reports whether Envoy restored the local address.
	IsLocalAddressRestored() bool
	// SetRemoteAddress overrides the effective downstream remote address.
	SetRemoteAddress(address string, port uint32, isIPv6 bool) bool
	// RestoreLocalAddress restores the downstream local address.
	RestoreLocalAddress(address string, port uint32, isIPv6 bool) bool

	// ContinueFilterChain resumes listener filter iteration after a stop result.
	ContinueFilterChain(success bool)
	// UseOriginalDst configures whether Envoy should honor the original destination.
	UseOriginalDst(useOriginalDst bool)
	// CloseSocket closes the downstream socket and records optional details.
	CloseSocket(details string)
	// WriteToSocket writes bytes directly to the downstream socket.
	WriteToSocket(data []byte) int64

	// GetSocketFD returns the socket file descriptor when Envoy exposes one.
	GetSocketFD() int64
	// SetSocketOptionInt sets an integer socket option on the downstream socket.
	SetSocketOptionInt(level, name, value int64) bool
	// SetSocketOptionBytes sets a bytes socket option on the downstream socket.
	SetSocketOptionBytes(level, name int64, value []byte) bool
	// GetSocketOptionInt returns an integer socket option value, if present.
	GetSocketOptionInt(level, name int64) (int64, bool)
	// GetSocketOptionBytes returns a bytes socket option value, if present.
	GetSocketOptionBytes(level, name int64, maxSize uint64) ([]byte, bool)

	// SetDynamicMetadataString sets a string dynamic metadata value.
	SetDynamicMetadataString(metadataNamespace, key, value string)
	// GetDynamicMetadataString returns a string dynamic metadata value, if present.
	GetDynamicMetadataString(metadataNamespace, key string) (UnsafeEnvoyBuffer, bool)
	// SetDynamicMetadataNumber sets a numeric dynamic metadata value.
	SetDynamicMetadataNumber(metadataNamespace, key string, value float64)
	// GetDynamicMetadataNumber returns a numeric dynamic metadata value, if present.
	GetDynamicMetadataNumber(metadataNamespace, key string) (float64, bool)

	// SetFilterState stores a raw bytes filter state value under key.
	SetFilterState(key string, value []byte) bool
	// GetFilterState returns the raw bytes filter state value stored under key, if present.
	GetFilterState(key string) (UnsafeEnvoyBuffer, bool)

	// SetDownstreamTransportFailureReason records the downstream transport failure reason.
	SetDownstreamTransportFailureReason(reason string)
	// GetConnectionStartTimeMs returns the downstream connection start time in milliseconds.
	GetConnectionStartTimeMs() uint64
	// GetCurrentMaxReadBytes returns Envoy's current max listener-filter peek size.
	GetCurrentMaxReadBytes() uint64

	// HttpCallout starts an asynchronous HTTP callout from the listener filter worker thread.
	HttpCallout(cluster string, headers [][2]string, body []byte, timeoutMs uint64,
		cb HttpCalloutCallback) (HttpCalloutInitResult, uint64)

	// RecordHistogramValue records value in a histogram metric defined from the config handle.
	RecordHistogramValue(id MetricID, value uint64) MetricsResult
	// SetGaugeValue sets a gauge metric to value.
	SetGaugeValue(id MetricID, value uint64) MetricsResult
	// IncrementGaugeValue increases a gauge metric by value.
	IncrementGaugeValue(id MetricID, value uint64) MetricsResult
	// DecrementGaugeValue decreases a gauge metric by value.
	DecrementGaugeValue(id MetricID, value uint64) MetricsResult
	// IncrementCounterValue increases a counter metric by value.
	IncrementCounterValue(id MetricID, value uint64) MetricsResult

	// GetScheduler returns a scheduler bound to this listener filter's worker thread.
	GetScheduler() Scheduler
	// GetWorkerIndex returns the Envoy worker index for this listener filter instance.
	GetWorkerIndex() uint32

	// Log writes a formatted message through Envoy's logging subsystem.
	Log(level LogLevel, format string, args ...any)
}

// ListenerFilterConfigHandle exposes host services during listener filter config creation.
type ListenerFilterConfigHandle interface {
	// DefineHistogram defines a histogram metric during config creation.
	DefineHistogram(name string) (MetricID, MetricsResult)
	// DefineGauge defines a gauge metric during config creation.
	DefineGauge(name string) (MetricID, MetricsResult)
	// DefineCounter defines a counter metric during config creation.
	DefineCounter(name string) (MetricID, MetricsResult)
	// GetScheduler returns a scheduler bound to the config context.
	GetScheduler() Scheduler
	// Log writes a formatted message through Envoy's logging subsystem.
	Log(level LogLevel, format string, args ...any)
}
