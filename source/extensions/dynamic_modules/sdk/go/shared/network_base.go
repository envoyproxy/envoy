package shared

// NetworkBuffer is an interface that provides access to the read and write buffers of a network
// connection. This should be implemented by the SDK or runtime.
type NetworkBuffer interface {
	// GetChunks returns the current buffer as Envoy-owned chunks.
	//
	// For a read buffer, the chunks are valid after the first OnRead callback for the lifetime of
	// the connection. For a write buffer, they are valid after the first OnWrite callback for the
	// lifetime of the connection. Copy the data if you need to retain it after the current callback.
	GetChunks() []UnsafeEnvoyBuffer

	// GetSize returns the total size of the current buffer in bytes.
	GetSize() uint64

	// Drain removes numBytes from the front of the buffer.
	//
	// It returns false when the underlying Envoy buffer is unavailable or the operation fails.
	Drain(numBytes uint64) bool

	// Prepend adds data to the front of the buffer.
	//
	// The provided bytes are owned by the caller for the duration of the call. It returns false if
	// Envoy rejects the operation.
	Prepend(data []byte) bool

	// Append adds data to the end of the buffer.
	//
	// The provided bytes are owned by the caller for the duration of the call. It returns false if
	// Envoy rejects the operation.
	Append(data []byte) bool
}

type SocketOptionValueType uint32

const (
	SocketOptionValueTypeInt SocketOptionValueType = iota
	SocketOptionValueTypeBytes
)

type SocketOption struct {
	Level      int64
	Name       int64
	State      SocketOptionState
	ValueType  SocketOptionValueType
	IntValue   int64
	BytesValue UnsafeEnvoyBuffer
}

type NetworkFilterStatus int32

const (
	NetworkFilterStatusContinue NetworkFilterStatus = 0
	NetworkFilterStatusStop     NetworkFilterStatus = 1
	NetworkFilterStatusDefault  NetworkFilterStatus = NetworkFilterStatusContinue
)

type NetworkConnectionCloseType uint32

const (
	NetworkConnectionCloseTypeFlushWrite NetworkConnectionCloseType = iota
	NetworkConnectionCloseTypeNoFlush
	NetworkConnectionCloseTypeFlushWriteAndDelay
	NetworkConnectionCloseTypeAbort
	NetworkConnectionCloseTypeAbortReset
)

type NetworkConnectionEvent uint32

const (
	NetworkConnectionEventRemoteClose NetworkConnectionEvent = iota
	NetworkConnectionEventLocalClose
	NetworkConnectionEventConnected
	NetworkConnectionEventConnectedZeroRTT
)

type NetworkConnectionState uint32

const (
	NetworkConnectionStateOpen NetworkConnectionState = iota
	NetworkConnectionStateClosing
	NetworkConnectionStateClosed
)

type NetworkReadDisableStatus uint32

const (
	NetworkReadDisableStatusNoTransition NetworkReadDisableStatus = iota
	NetworkReadDisableStatusStillReadDisabled
	NetworkReadDisableStatusTransitionedToReadEnabled
	NetworkReadDisableStatusTransitionedToReadDisabled
)

// NetworkFilterHandle is the interface that provides access to the network filter's connection
// state and related host features. This should be implemented by the SDK or runtime.
type NetworkFilterHandle interface {
	// ReadBuffer returns the current downstream -> upstream buffer for this connection.
	ReadBuffer() NetworkBuffer

	// WriteBuffer returns the current upstream -> downstream buffer for this connection.
	WriteBuffer() NetworkBuffer

	// Write writes data directly to the downstream connection.
	//
	// When endStream is true, Envoy half-closes the connection after writing.
	Write(data []byte, endStream bool)

	// InjectReadData injects data into the read filter chain after this filter.
	//
	// The injected bytes continue through later read filters as if they had just been received.
	InjectReadData(data []byte, endStream bool)

	// InjectWriteData injects data into the write filter chain after this filter.
	//
	// The injected bytes continue through later write filters as if they had just been produced.
	InjectWriteData(data []byte, endStream bool)

	// ContinueReading resumes filter iteration after OnNewConnection, OnRead, or OnWrite previously
	// returned NetworkFilterStatusStop.
	ContinueReading()

	// Close closes the connection using the supplied close behavior.
	Close(closeType NetworkConnectionCloseType)

	// GetConnectionID returns the unique Envoy connection ID.
	GetConnectionID() uint64

	// GetRemoteAddress returns the remote peer address and port as seen by Envoy.
	GetRemoteAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// GetLocalAddress returns the local address and port for the connection.
	GetLocalAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// IsSSL reports whether the connection is using SSL/TLS.
	IsSSL() bool

	// DisableClose disables or re-enables close handling for this filter instance.
	DisableClose(disabled bool)

	// CloseWithDetails closes the connection and records termination details that Envoy can surface
	// in logs and diagnostics.
	CloseWithDetails(closeType NetworkConnectionCloseType, details string)

	// GetRequestedServerName returns the requested server name (SNI) from the TLS handshake.
	GetRequestedServerName() (UnsafeEnvoyBuffer, bool)

	// GetDirectRemoteAddress returns the direct remote address and port without proxy/XFF handling.
	GetDirectRemoteAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// GetSSLURISANs returns the peer certificate URI SAN entries.
	GetSSLURISANs() []UnsafeEnvoyBuffer

	// GetSSLDNSSANs returns the peer certificate DNS SAN entries.
	GetSSLDNSSANs() []UnsafeEnvoyBuffer

	// GetSSLSubject returns the peer certificate subject.
	GetSSLSubject() (UnsafeEnvoyBuffer, bool)

	// SetFilterState stores a raw bytes filter state value under key.
	//
	// Other filters can read the value, and built-in filters may use it for routing decisions.
	SetFilterState(key string, value []byte) bool

	// GetFilterState returns a raw bytes filter state value previously stored under key.
	GetFilterState(key string) (UnsafeEnvoyBuffer, bool)

	// SetFilterStateTyped stores a typed filter state value using Envoy's registered ObjectFactory
	// for key.
	SetFilterStateTyped(key string, value []byte) bool

	// GetFilterStateTyped returns the serialized bytes of the typed filter state object stored under
	// key.
	GetFilterStateTyped(key string) (UnsafeEnvoyBuffer, bool)

	// GetMetadataString returns a dynamic metadata string value for metadataNamespace/key.
	GetMetadataString(metadataNamespace, key string) (UnsafeEnvoyBuffer, bool)

	// GetMetadataNumber returns a dynamic metadata number value for metadataNamespace/key.
	GetMetadataNumber(metadataNamespace, key string) (float64, bool)

	// GetMetadataBool returns a dynamic metadata bool value for metadataNamespace/key.
	GetMetadataBool(metadataNamespace, key string) (bool, bool)

	// SetMetadata stores a string, number, or bool dynamic metadata value at
	// metadataNamespace/key, replacing any existing value.
	SetMetadata(metadataNamespace, key string, value any)

	// SetSocketOptionInt stores an integer socket option to be applied at the specified socket
	// state.
	SetSocketOptionInt(level, name int64, state SocketOptionState, value int64)

	// SetSocketOptionBytes stores a bytes socket option to be applied at the specified socket state.
	SetSocketOptionBytes(level, name int64, state SocketOptionState, value []byte)

	// GetSocketOptionInt returns an integer socket option value if one is present for the supplied
	// level/name/state tuple.
	GetSocketOptionInt(level, name int64, state SocketOptionState) (int64, bool)

	// GetSocketOptionBytes returns a bytes socket option value if one is present for the supplied
	// level/name/state tuple.
	GetSocketOptionBytes(level, name int64, state SocketOptionState) (UnsafeEnvoyBuffer, bool)

	// GetSocketOptions returns all socket options currently stored on the connection.
	GetSocketOptions() []SocketOption

	// Log writes a formatted message through Envoy's logging subsystem.
	Log(level LogLevel, format string, args ...any)

	// HttpCallout performs an HTTP call to an external service. The call is asynchronous, and the
	// response will be delivered via the provided callback.
	//
	// NOTE: This method should only be called during network filter callbacks or scheduled functions
	// via the Scheduler so that it runs on the filter's worker thread.
	HttpCallout(cluster string, headers [][2]string, body []byte, timeoutMs uint64,
		cb HttpCalloutCallback) (HttpCalloutInitResult, uint64)

	// RecordHistogramValue records value in a histogram metric previously defined from the config
	// handle.
	RecordHistogramValue(id MetricID, value uint64) MetricsResult

	// SetGaugeValue sets a gauge metric to value.
	SetGaugeValue(id MetricID, value uint64) MetricsResult

	// IncrementGaugeValue increases a gauge metric by value.
	IncrementGaugeValue(id MetricID, value uint64) MetricsResult

	// DecrementGaugeValue decreases a gauge metric by value.
	DecrementGaugeValue(id MetricID, value uint64) MetricsResult

	// IncrementCounterValue increases a counter metric by value.
	IncrementCounterValue(id MetricID, value uint64) MetricsResult

	// GetClusterHostCounts returns the total, healthy, and degraded host counts for the named
	// cluster and priority.
	GetClusterHostCounts(cluster string, priority uint32) (ClusterHostCounts, bool)

	// GetUpstreamHostAddress returns the currently selected upstream host address and port.
	GetUpstreamHostAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// GetUpstreamHostHostname returns the currently selected upstream host hostname.
	GetUpstreamHostHostname() (UnsafeEnvoyBuffer, bool)

	// GetUpstreamHostCluster returns the cluster name of the currently selected upstream host.
	GetUpstreamHostCluster() (UnsafeEnvoyBuffer, bool)

	// HasUpstreamHost reports whether Envoy has already selected an upstream host for this
	// connection.
	HasUpstreamHost() bool

	// StartUpstreamSecureTransport upgrades the upstream connection into secure mode when the
	// transport socket supports StartTLS-style promotion.
	StartUpstreamSecureTransport() bool

	// GetConnectionState returns the current connection state.
	GetConnectionState() NetworkConnectionState

	// ReadDisable disables or re-enables reading on the connection.
	//
	// Envoy reference-counts these calls; callers must balance disables with enables.
	ReadDisable(disable bool) NetworkReadDisableStatus

	// ReadEnabled reports whether reading is currently enabled.
	ReadEnabled() bool

	// IsHalfCloseEnabled reports whether remote half-close leaves the connection writable.
	IsHalfCloseEnabled() bool

	// EnableHalfClose enables or disables half-close semantics on the connection.
	EnableHalfClose(enabled bool)

	// GetBufferLimit returns the current soft buffer limit in bytes.
	GetBufferLimit() uint32

	// SetBufferLimits updates the soft buffer limit in bytes.
	//
	// On the write side, this controls high/low watermark transitions surfaced through the
	// watermark callbacks.
	SetBufferLimits(limit uint32)

	// AboveHighWatermark reports whether the write buffer is currently above the high watermark.
	AboveHighWatermark() bool

	// GetScheduler retrieves the scheduler related to this network filter for asynchronous
	// operations.
	//
	// NOTE: This MUST only be called during network filter callbacks. But then the returned
	// Scheduler can be used later even outside of the callbacks and at other threads.
	GetScheduler() Scheduler

	// GetWorkerIndex returns the worker index assigned to the current filter instance.
	GetWorkerIndex() uint32
}

type NetworkFilterConfigHandle interface {
	// Log writes a formatted message through Envoy's logging subsystem.
	Log(level LogLevel, format string, args ...any)

	// DefineHistogram creates a histogram metric during configuration initialization and returns its
	// opaque ID.
	DefineHistogram(name string) (MetricID, MetricsResult)

	// DefineGauge creates a gauge metric during configuration initialization and returns its opaque
	// ID.
	DefineGauge(name string) (MetricID, MetricsResult)

	// DefineCounter creates a counter metric during configuration initialization and returns its
	// opaque ID.
	DefineCounter(name string) (MetricID, MetricsResult)

	// GetScheduler retrieves a scheduler for deferred task execution in the config context.
	// This should be called only during the plugin configuration phase, and the returned
	// Scheduler can be used later even outside of the callbacks and at other threads.
	GetScheduler() Scheduler
}
