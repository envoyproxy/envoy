//go:generate mockgen -source=network.go -destination=mocks/mock_network.go -package=mocks
package shared

// Network filter SDK surface for dynamic modules.
//
// This mirrors the Rust SDK's `network` module. A module that exposes a network filter implements
// NetworkFilterConfigFactory, registers it from an init() function via
// sdk.RegisterNetworkFilterConfigFactories, and Envoy will create a NetworkFilter for each TCP
// connection that traverses the listener.
//
// All event hooks on a single NetworkFilter are called on the same worker thread that the filter
// was created on (i.e., the worker thread that handled the corresponding accept). Methods on
// NetworkFilterHandle MUST only be called from that worker thread, either inside an OnXxx callback
// or from a function scheduled via the Scheduler returned by NetworkFilterHandle.GetScheduler.

// NetworkFilterDataStatus is the status returned by OnNewConnection, OnRead, and OnWrite. It
// corresponds to envoy_dynamic_module_type_on_network_filter_data_status in the dynamic-module
// ABI, which in turn maps to Envoy's `Network::FilterStatus`.
type NetworkFilterDataStatus uint32

const (
	// NetworkFilterDataStatusContinue allows further filters in the chain to run for the same
	// event.
	NetworkFilterDataStatusContinue NetworkFilterDataStatus = iota
	// NetworkFilterDataStatusStopIteration stops further filters from running. For OnRead, no
	// more data will be read from the connection until the module calls
	// NetworkFilterHandle.ContinueReading.
	NetworkFilterDataStatusStopIteration
	// NetworkFilterDataStatusDefault is the default value returned by EmptyNetworkFilter and
	// equals NetworkFilterDataStatusContinue.
	NetworkFilterDataStatusDefault NetworkFilterDataStatus = NetworkFilterDataStatusContinue
)

// NetworkConnectionCloseType controls how a connection is closed. It corresponds to
// envoy_dynamic_module_type_network_connection_close_type / Envoy's
// `Network::ConnectionCloseType`.
type NetworkConnectionCloseType uint32

const (
	// NetworkConnectionCloseTypeFlushWrite flushes pending write data before raising
	// ConnectionEvent::LocalClose.
	NetworkConnectionCloseTypeFlushWrite NetworkConnectionCloseType = iota
	// NetworkConnectionCloseTypeNoFlush does not flush pending data; it writes the pending data to
	// the transport and immediately raises ConnectionEvent::LocalClose.
	NetworkConnectionCloseTypeNoFlush
	// NetworkConnectionCloseTypeFlushWriteAndDelay flushes pending write data and delays raising
	// ConnectionEvent::LocalClose until the delayed-close timeout expires.
	NetworkConnectionCloseTypeFlushWriteAndDelay
	// NetworkConnectionCloseTypeAbort does not write pending data and immediately raises
	// ConnectionEvent::LocalClose.
	NetworkConnectionCloseTypeAbort
	// NetworkConnectionCloseTypeAbortReset does not write pending data, sends RST, and
	// immediately raises ConnectionEvent::LocalClose.
	NetworkConnectionCloseTypeAbortReset
)

// NetworkConnectionEvent is the lifecycle event delivered to NetworkFilter.OnEvent. It corresponds
// to envoy_dynamic_module_type_network_connection_event / Envoy's `Network::ConnectionEvent`.
type NetworkConnectionEvent uint32

const (
	// NetworkConnectionEventRemoteClose indicates the remote peer closed the connection.
	NetworkConnectionEventRemoteClose NetworkConnectionEvent = iota
	// NetworkConnectionEventLocalClose indicates Envoy closed the connection locally.
	NetworkConnectionEventLocalClose
	// NetworkConnectionEventConnected indicates the connection has been established.
	NetworkConnectionEventConnected
	// NetworkConnectionEventConnectedZeroRtt indicates the connection has been established using
	// 0-RTT data.
	NetworkConnectionEventConnectedZeroRtt
)

// NetworkConnectionState is the result of NetworkFilterHandle.GetConnectionState. It corresponds
// to envoy_dynamic_module_type_network_connection_state / Envoy's `Network::Connection::State`.
type NetworkConnectionState uint32

const (
	// NetworkConnectionStateOpen — connection is open and active.
	NetworkConnectionStateOpen NetworkConnectionState = iota
	// NetworkConnectionStateClosing — connection is in the process of closing (e.g., draining
	// pending writes).
	NetworkConnectionStateClosing
	// NetworkConnectionStateClosed — connection has been closed.
	NetworkConnectionStateClosed
)

// NetworkReadDisableStatus is the result of NetworkFilterHandle.ReadDisable. It corresponds to
// envoy_dynamic_module_type_network_read_disable_status / Envoy's
// `Network::Connection::ReadDisableStatus`.
type NetworkReadDisableStatus uint32

const (
	// NetworkReadDisableStatusNoTransition indicates no state change occurred (e.g., reads were
	// already disabled and the call further incremented the disable counter).
	NetworkReadDisableStatusNoTransition NetworkReadDisableStatus = iota
	// NetworkReadDisableStatusStillReadDisabled indicates reads remain disabled because at least
	// one other source still has reads disabled.
	NetworkReadDisableStatusStillReadDisabled
	// NetworkReadDisableStatusTransitionedToReadEnabled indicates the call moved the connection
	// from read-disabled to read-enabled.
	NetworkReadDisableStatusTransitionedToReadEnabled
	// NetworkReadDisableStatusTransitionedToReadDisabled indicates the call moved the connection
	// from read-enabled to read-disabled.
	NetworkReadDisableStatusTransitionedToReadDisabled
)

// SocketOptionValue is the value of a SocketOption — either an integer or a byte slice.
// IsBytes selects the active variant.
type SocketOptionValue struct {
	// IsBytes is true when the value is held in Bytes; false when it is held in Int.
	IsBytes bool
	// Int holds the integer value. Valid only when IsBytes is false.
	Int int64
	// Bytes holds the bytes value. Valid only when IsBytes is true.
	// NOTE: When returned from a getter, the buffer's memory may be owned by Envoy and only valid
	// for the duration of the current callback. Copy if you need to keep it.
	Bytes UnsafeEnvoyBuffer
}

// SocketOption describes a single socket option attached to a connection. Returned by
// NetworkFilterHandle.GetSocketOptions.
type SocketOption struct {
	// Level is the socket option level (e.g., SOL_SOCKET, IPPROTO_TCP).
	Level int64
	// Name is the socket option name (e.g., SO_KEEPALIVE, TCP_NODELAY).
	Name int64
	// State is the connection state at which this option should be applied.
	State SocketOptionState
	// Value is the option value (integer or bytes; see SocketOptionValue.IsBytes).
	Value SocketOptionValue
}

// NetworkFilter is the per-connection module-side filter object. A new instance is created for
// each TCP connection by NetworkFilterFactory.Create and destroyed when the connection ends.
//
// All event hooks on a given NetworkFilter are called on the same worker thread on which the
// filter was created. Methods on the NetworkFilterHandle passed to each callback MUST only be
// called on that thread.
//
// Embed EmptyNetworkFilter to opt out of any callbacks you don't care about.
type NetworkFilter interface {
	// OnNewConnection is called when the new TCP connection is established, before any data
	// flows. Returning NetworkFilterDataStatusStopIteration halts the filter chain until
	// NetworkFilterHandle.ContinueReading is called.
	OnNewConnection(handle NetworkFilterHandle) NetworkFilterDataStatus

	// OnRead is called when data is read from the downstream (client → upstream direction).
	// dataLength is the total length of the read buffer at this point. Use
	// NetworkFilterHandle.GetReadBufferChunks to inspect the data. endOfStream is true when the
	// downstream has half-closed.
	//
	// Returning NetworkFilterDataStatusStopIteration halts further reading until
	// NetworkFilterHandle.ContinueReading is called.
	OnRead(handle NetworkFilterHandle, dataLength uint64, endOfStream bool) NetworkFilterDataStatus

	// OnWrite is called when data is to be written to the downstream (upstream → client
	// direction). dataLength is the total length of the write buffer. endOfStream is true when
	// the connection is being closed after this write.
	OnWrite(handle NetworkFilterHandle, dataLength uint64, endOfStream bool) NetworkFilterDataStatus

	// OnEvent is called for connection lifecycle events (Connected, RemoteClose, LocalClose,
	// etc.). See NetworkConnectionEvent for the list of events.
	OnEvent(handle NetworkFilterHandle, event NetworkConnectionEvent)

	// OnAboveWriteBufferHighWatermark is called when the connection's write buffer has exceeded
	// the high watermark. A typical implementation calls handle.ReadDisable(true) to apply
	// back-pressure to the downstream until the write buffer drains.
	OnAboveWriteBufferHighWatermark(handle NetworkFilterHandle)

	// OnBelowWriteBufferLowWatermark is called when the write buffer has dropped below the low
	// watermark after previously being above the high watermark. A typical implementation calls
	// handle.ReadDisable(false) to resume reading.
	OnBelowWriteBufferLowWatermark(handle NetworkFilterHandle)

	// OnDestroy is called when the filter is being destroyed, after the connection has ended.
	// No NetworkFilterHandle is passed because the underlying Envoy filter pointer is no longer
	// valid by the time this hook fires; modules that need handle access during teardown should
	// do their work in OnEvent(LocalClose|RemoteClose) instead.
	//
	// This is the final callback for this filter instance. It runs on the same worker thread as
	// the other callbacks.
	OnDestroy()
}

// EmptyNetworkFilter is a no-op NetworkFilter that returns NetworkFilterDataStatusDefault for
// every data callback and ignores everything else. Embed it into your own filter to opt out of
// callbacks you don't need.
type EmptyNetworkFilter struct{}

func (*EmptyNetworkFilter) OnNewConnection(_ NetworkFilterHandle) NetworkFilterDataStatus {
	return NetworkFilterDataStatusDefault
}
func (*EmptyNetworkFilter) OnRead(_ NetworkFilterHandle, _ uint64, _ bool) NetworkFilterDataStatus {
	return NetworkFilterDataStatusDefault
}
func (*EmptyNetworkFilter) OnWrite(_ NetworkFilterHandle, _ uint64, _ bool) NetworkFilterDataStatus {
	return NetworkFilterDataStatusDefault
}
func (*EmptyNetworkFilter) OnEvent(_ NetworkFilterHandle, _ NetworkConnectionEvent) {}
func (*EmptyNetworkFilter) OnAboveWriteBufferHighWatermark(_ NetworkFilterHandle)   {}
func (*EmptyNetworkFilter) OnBelowWriteBufferLowWatermark(_ NetworkFilterHandle)    {}
func (*EmptyNetworkFilter) OnDestroy()                                              {}

// NetworkFilterFactory creates per-connection NetworkFilter instances. It holds the parsed
// configuration and is shared across all connections handled by the same listener configuration.
//
// Implementations must be safe for concurrent calls: Envoy may call Create from multiple worker
// threads simultaneously.
type NetworkFilterFactory interface {
	// Create creates a NetworkFilter for a new TCP connection. The handle passed in is bound to
	// the new connection's worker thread.
	Create(handle NetworkFilterHandle) NetworkFilter

	// OnDestroy is called when the factory is destroyed (typically on configuration update,
	// after all in-flight connections using this factory have ended). A good place to release
	// per-config resources.
	OnDestroy()
}

// EmptyNetworkFilterFactory is a no-op NetworkFilterFactory that returns an EmptyNetworkFilter
// for every Create call.
type EmptyNetworkFilterFactory struct{}

func (*EmptyNetworkFilterFactory) Create(_ NetworkFilterHandle) NetworkFilter {
	return &EmptyNetworkFilter{}
}
func (*EmptyNetworkFilterFactory) OnDestroy() {}

// NetworkFilterConfigFactory is the top-level factory the module registers via
// sdk.RegisterNetworkFilterConfigFactories. Envoy calls Create exactly once per filter
// configuration to produce a NetworkFilterFactory; that factory is then used for every connection
// handled by that listener configuration.
//
// Implementations should be stateless or hold only configuration-derived state.
type NetworkFilterConfigFactory interface {
	// Create parses unparsedConfig (typically a serialized proto) and returns a factory.
	// Returning a non-nil error rejects the configuration: the listener that referenced it will
	// fail to load.
	Create(handle NetworkFilterConfigHandle, unparsedConfig []byte) (NetworkFilterFactory, error)
}

// EmptyNetworkFilterConfigFactory is a no-op NetworkFilterConfigFactory that returns an
// EmptyNetworkFilterFactory regardless of input.
type EmptyNetworkFilterConfigFactory struct{}

func (*EmptyNetworkFilterConfigFactory) Create(_ NetworkFilterConfigHandle, _ []byte) (NetworkFilterFactory, error) {
	return &EmptyNetworkFilterFactory{}, nil
}

// NetworkFilterConfigHandle is the config-context handle. It is valid for the lifetime of the
// NetworkFilterFactory and is used to define metrics scoped to the filter configuration and to
// schedule events on the main thread.
type NetworkFilterConfigHandle interface {
	// DefineCounter creates a per-config counter with the given name. The returned MetricID can
	// be used with NetworkFilterHandle.IncrementCounter on any connection handled by this
	// configuration.
	DefineCounter(name string) (MetricID, MetricsResult)

	// DefineGauge creates a per-config gauge with the given name. See DefineCounter.
	DefineGauge(name string) (MetricID, MetricsResult)

	// DefineHistogram creates a per-config histogram with the given name. See DefineCounter.
	DefineHistogram(name string) (MetricID, MetricsResult)

	// GetScheduler returns a scheduler bound to the main thread. The returned Scheduler is safe
	// to use from any goroutine; functions scheduled on it will run on Envoy's main thread.
	//
	// Use this to dispatch configuration-level work (e.g., re-loading external state) onto the
	// main thread from a background worker.
	GetScheduler() Scheduler
}

// NetworkFilterHandle is the per-connection handle used for all operations on the connection
// from inside the module. Methods on this handle MUST only be called on the worker thread that
// owns the connection — i.e., from inside an OnXxx callback or from a function scheduled via
// GetScheduler.
type NetworkFilterHandle interface {
	// ---- read/write buffer access ----

	// GetReadBufferChunks returns the chunks of the current read buffer. Valid only inside or
	// after the first OnRead callback.
	//
	// NOTE: The memory is owned by Envoy and is only valid for the duration of the current
	// callback. Copy if you need to keep it.
	GetReadBufferChunks() []UnsafeEnvoyBuffer

	// GetReadBufferSize returns the total byte size of the current read buffer.
	GetReadBufferSize() uint64

	// GetWriteBufferChunks returns the chunks of the current write buffer. Valid only inside or
	// after the first OnWrite callback.
	//
	// NOTE: The memory is owned by Envoy and is only valid for the duration of the current
	// callback. Copy if you need to keep it.
	GetWriteBufferChunks() []UnsafeEnvoyBuffer

	// GetWriteBufferSize returns the total byte size of the current write buffer.
	GetWriteBufferSize() uint64

	// DrainReadBuffer drops length bytes from the front of the read buffer. Returns true on
	// success.
	DrainReadBuffer(length uint64) bool

	// DrainWriteBuffer drops length bytes from the front of the write buffer. Returns true on
	// success.
	DrainWriteBuffer(length uint64) bool

	// PrependReadBuffer prepends data to the front of the read buffer. Returns true on success.
	PrependReadBuffer(data []byte) bool

	// AppendReadBuffer appends data to the back of the read buffer. Returns true on success.
	AppendReadBuffer(data []byte) bool

	// PrependWriteBuffer prepends data to the front of the write buffer. Returns true on success.
	PrependWriteBuffer(data []byte) bool

	// AppendWriteBuffer appends data to the back of the write buffer. Returns true on success.
	AppendWriteBuffer(data []byte) bool

	// Write writes data directly to the downstream connection. If endOfStream is true, the
	// connection is half-closed after the write.
	Write(data []byte, endOfStream bool)

	// InjectReadData injects data into the read filter chain after this filter. Subsequent
	// filters will see the injected data as if it had been read from the downstream.
	InjectReadData(data []byte, endOfStream bool)

	// InjectWriteData injects data into the write filter chain after this filter. Subsequent
	// filters will see the injected data as if it had been written from the upstream.
	InjectWriteData(data []byte, endOfStream bool)

	// ContinueReading resumes reading after the filter previously returned
	// NetworkFilterDataStatusStopIteration from OnNewConnection or OnRead.
	ContinueReading()

	// ---- connection control ----

	// Close closes the connection using the given closeType. See NetworkConnectionCloseType for
	// the available close strategies.
	Close(closeType NetworkConnectionCloseType)

	// CloseWithDetails closes the connection with a closeType and a free-form details string
	// that is recorded as the connection's termination reason for logging.
	CloseWithDetails(closeType NetworkConnectionCloseType, details string)

	// DisableClose disables (or re-enables) connection close handling for this filter. While
	// disabled, the filter will not be torn down even if the connection's close conditions
	// would otherwise apply.
	DisableClose(disabled bool)

	// GetConnectionID returns the unique connection ID assigned by Envoy.
	GetConnectionID() uint64

	// GetConnectionState returns the current state of the connection (Open / Closing / Closed).
	GetConnectionState() NetworkConnectionState

	// ReadDisable disables (or re-enables) reading on the connection. This is the primary
	// mechanism for implementing back-pressure in TCP filters.
	//
	// When reads are disabled, no more data will be read from the socket. When re-enabled, if
	// there is data in the input buffer, it will be re-dispatched through the filter chain.
	//
	// Calls are reference-counted. For example:
	//
	//   handle.ReadDisable(true)   // disables reading
	//   handle.ReadDisable(true)   // notes the connection is blocked by two sources
	//   handle.ReadDisable(false)  // notes the connection is blocked by one source
	//   handle.ReadDisable(false)  // marks the connection as unblocked, resumes reading
	//
	// The returned NetworkReadDisableStatus reports the resulting state transition.
	ReadDisable(disable bool) NetworkReadDisableStatus

	// ReadEnabled returns true if reading is currently enabled on the connection.
	ReadEnabled() bool

	// IsHalfCloseEnabled reports whether half-close semantics are enabled for the connection.
	// When half-close is enabled, reading a remote half-close will not fully close the
	// connection.
	IsHalfCloseEnabled() bool

	// EnableHalfClose enables or disables half-close semantics on the connection. When enabled,
	// reading a remote half-close will not fully close the connection, allowing the filter to
	// continue writing data.
	EnableHalfClose(enabled bool)

	// GetBufferLimit returns the current per-connection buffer limit.
	GetBufferLimit() uint32

	// SetBufferLimits sets a soft limit on the read/write buffers for the connection.
	//
	// For the read buffer, this limits the bytes read prior to flushing to further stages in
	// the processing pipeline. For the write buffer, it sets watermarks. When enough data is
	// buffered, NetworkFilter.OnAboveWriteBufferHighWatermark is invoked. When enough data is
	// drained from the write buffer, NetworkFilter.OnBelowWriteBufferLowWatermark is invoked.
	SetBufferLimits(limit uint32)

	// AboveHighWatermark returns true if the connection is currently above its high watermark.
	AboveHighWatermark() bool

	// ---- connection metadata ----

	// GetRemoteAddress returns the downstream remote (client) address and port. The third
	// return value is true when the address was found and is an IP address.
	GetRemoteAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// GetLocalAddress returns the local (Envoy-side) address and port for the connection.
	GetLocalAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// GetDirectRemoteAddress returns the direct remote (client) address — the peer address
	// before any listener-filter modification (e.g., proxy-protocol parsing).
	GetDirectRemoteAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// GetRequestedServerName returns the SNI from the TLS handshake. Returns false if the
	// connection is not TLS or no SNI was sent.
	//
	// NOTE: The buffer is owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep it.
	GetRequestedServerName() (UnsafeEnvoyBuffer, bool)

	// IsSSL reports whether the connection uses TLS.
	IsSSL() bool

	// GetSSLURISans returns the URI Subject Alternative Names from the peer certificate. Returns
	// an empty slice if the connection is not SSL or no URI SANs are present.
	//
	// NOTE: Each buffer is owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep them.
	GetSSLURISans() []UnsafeEnvoyBuffer

	// GetSSLDNSSans returns the DNS Subject Alternative Names from the peer certificate. Returns
	// an empty slice if the connection is not SSL or no DNS SANs are present.
	//
	// NOTE: Each buffer is owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep them.
	GetSSLDNSSans() []UnsafeEnvoyBuffer

	// GetSSLSubject returns the subject of the peer certificate. Returns false if the connection
	// is not SSL or the subject is not available.
	//
	// NOTE: The buffer is owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep it.
	GetSSLSubject() (UnsafeEnvoyBuffer, bool)

	// ---- filter state ----

	// SetFilterState stores a bytes value under key in the connection's filter state. The
	// filter state can be read by other filters in the chain and can influence routing
	// decisions (e.g., tcp_proxy cluster selection).
	//
	// Returns true on success.
	SetFilterState(key string, value []byte) bool

	// GetFilterState retrieves a bytes value previously stored under key. Returns false if no
	// value is stored for the key.
	//
	// NOTE: The buffer is owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep it.
	GetFilterState(key string) (UnsafeEnvoyBuffer, bool)

	// SetFilterStateTyped stores a typed filter state value. The key MUST match a registered
	// `StreamInfo::FilterState::ObjectFactory` on the Envoy side; the bytes are passed to that
	// factory's createFromBytes. This is required for interoperability with built-in Envoy
	// filters that read filter state as typed objects (e.g., tcp_proxy reads
	// `PerConnectionCluster` via the key "envoy.tcp_proxy.cluster").
	//
	// Returns false if no factory is registered for the key, the factory fails to create the
	// object, or the key already exists and is marked as read-only.
	SetFilterStateTyped(key string, value []byte) bool

	// GetFilterStateTyped retrieves the serialized bytes of a typed filter state object stored
	// under key. The object must support `serializeAsString()` on the Envoy side. Returns false
	// if the key does not exist, the object does not support serialization, or the filter state
	// is not accessible.
	//
	// NOTE: The buffer is owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep it.
	GetFilterStateTyped(key string) (UnsafeEnvoyBuffer, bool)

	// ---- dynamic metadata ----

	// SetDynamicMetadataString sets the string-typed dynamic metadata value at the given
	// namespace and key. If a value already exists at that location it is overwritten.
	SetDynamicMetadataString(metadataNamespace, key, value string)

	// GetDynamicMetadataString retrieves the string-typed dynamic metadata value at the given
	// namespace and key. Returns false if the namespace or key does not exist or the value is
	// not a string.
	//
	// NOTE: The buffer is owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep it.
	GetDynamicMetadataString(metadataNamespace, key string) (UnsafeEnvoyBuffer, bool)

	// SetDynamicMetadataNumber sets the number-typed dynamic metadata value at the given
	// namespace and key. If a value already exists at that location it is overwritten.
	SetDynamicMetadataNumber(metadataNamespace, key string, value float64)

	// GetDynamicMetadataNumber retrieves the number-typed dynamic metadata value at the given
	// namespace and key. Returns false if the namespace or key does not exist or the value is
	// not a number.
	GetDynamicMetadataNumber(metadataNamespace, key string) (float64, bool)

	// SetDynamicMetadataBool sets the bool-typed dynamic metadata value at the given namespace
	// and key. If a value already exists at that location it is overwritten.
	SetDynamicMetadataBool(metadataNamespace, key string, value bool)

	// GetDynamicMetadataBool retrieves the bool-typed dynamic metadata value at the given
	// namespace and key. Returns false if the namespace or key does not exist or the value is
	// not a bool.
	GetDynamicMetadataBool(metadataNamespace, key string) (bool, bool)

	// ---- socket options ----

	// SetSocketOptionInt sets an integer-valued socket option on the connection.
	// level / name follow the same conventions as the libc setsockopt(2).
	SetSocketOptionInt(level, name int64, state SocketOptionState, value int64)

	// SetSocketOptionBytes sets a bytes-valued socket option on the connection.
	// level / name follow the same conventions as the libc setsockopt(2).
	SetSocketOptionBytes(level, name int64, state SocketOptionState, value []byte)

	// GetSocketOptionInt retrieves an integer socket option value previously set on the
	// connection. Returns false if no such option is stored.
	GetSocketOptionInt(level, name int64, state SocketOptionState) (int64, bool)

	// GetSocketOptionBytes retrieves a bytes socket option value previously set on the
	// connection. Returns false if no such option is stored.
	//
	// NOTE: The buffer is owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep it.
	GetSocketOptionBytes(level, name int64, state SocketOptionState) (UnsafeEnvoyBuffer, bool)

	// GetSocketOptions returns all socket options currently attached to the connection.
	GetSocketOptions() []SocketOption

	// ---- HTTP callouts ----

	// HttpCallout sends an asynchronous HTTP request against the named cluster. headers must
	// include :method, :path, and host. The result is delivered asynchronously via the supplied
	// HttpCalloutCallback.
	//
	// Multiple callouts can be in-flight simultaneously from a single filter; each is
	// distinguished by the returned callout ID. The callback object you pass MUST remain valid
	// at least until OnHttpCalloutDone is called for that callout.
	//
	// The HttpCalloutInitResult indicates whether initiation succeeded:
	//
	//   - HttpCalloutInitSuccess          — the callout was scheduled.
	//   - HttpCalloutInitMissingRequiredHeaders — :method, :path, or host was missing.
	//   - HttpCalloutInitClusterNotFound  — clusterName did not match any cluster.
	//   - HttpCalloutInitDuplicateCalloutId    — Envoy could not allocate a unique callout ID.
	//   - HttpCalloutInitCannotCreateRequest   — request could not be created (e.g., no healthy
	//     upstream host in the cluster).
	//
	// On any non-Success result, the returned callout ID is 0 and OnHttpCalloutDone will NOT be
	// invoked.
	HttpCallout(clusterName string, headers [][2]string, body []byte, timeoutMs uint64,
		cb HttpCalloutCallback) (HttpCalloutInitResult, uint64)

	// ---- metrics ----

	// IncrementCounter increments the counter previously defined via
	// NetworkFilterConfigHandle.DefineCounter.
	IncrementCounter(id MetricID, value uint64) MetricsResult

	// SetGauge sets the gauge previously defined via NetworkFilterConfigHandle.DefineGauge.
	SetGauge(id MetricID, value uint64) MetricsResult

	// IncrementGauge adds value to the gauge previously defined via
	// NetworkFilterConfigHandle.DefineGauge.
	IncrementGauge(id MetricID, value uint64) MetricsResult

	// DecrementGauge subtracts value from the gauge previously defined via
	// NetworkFilterConfigHandle.DefineGauge.
	DecrementGauge(id MetricID, value uint64) MetricsResult

	// RecordHistogramValue records value in the histogram previously defined via
	// NetworkFilterConfigHandle.DefineHistogram.
	RecordHistogramValue(id MetricID, value uint64) MetricsResult

	// ---- upstream/cluster info ----

	// GetClusterHostCount returns host counts for the named cluster at the given priority
	// level. priority of 0 selects the default priority. Returns false if the cluster is not
	// found or the priority level does not exist.
	//
	// Useful for implementing scale-to-zero logic or custom load-balancing decisions.
	GetClusterHostCount(clusterName string, priority uint32) (ClusterHostCount, bool)

	// GetUpstreamHostAddress returns the address and port of the currently selected upstream
	// host. Returns false if no upstream host has been selected or the address is not an IP.
	GetUpstreamHostAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// GetUpstreamHostHostname returns the hostname of the currently selected upstream host.
	// Returns false if no upstream host has been selected or the hostname is empty.
	GetUpstreamHostHostname() (UnsafeEnvoyBuffer, bool)

	// GetUpstreamHostCluster returns the cluster name of the currently selected upstream host.
	// Returns false if no upstream host has been selected.
	GetUpstreamHostCluster() (UnsafeEnvoyBuffer, bool)

	// HasUpstreamHost reports whether an upstream host has been selected for this connection.
	HasUpstreamHost() bool

	// StartUpstreamSecureTransport signals the filter manager to enable secure transport mode
	// in the upstream connection. This is only valid when the upstream transport socket is of
	// the StartTLS type — that is the only transport socket that can be programmatically
	// converted from non-secure to secure mode at runtime.
	//
	// Returns true if the upstream transport was successfully converted to secure mode.
	StartUpstreamSecureTransport() bool

	// ---- scheduling / misc ----

	// GetScheduler returns a scheduler bound to this filter's worker thread. The returned
	// Scheduler is safe to retain and use from any goroutine: tasks scheduled on it will run
	// on this filter's worker thread, allowing background work to safely re-enter
	// NetworkFilterHandle methods.
	//
	// Tasks are delivered to NetworkFilter callbacks (and to handle methods invoked from those
	// callbacks) IF the filter is still alive when the task fires. If the connection has been
	// destroyed in the meantime, scheduled tasks are dropped silently.
	//
	// Example:
	//
	//   func (f *MyFilter) OnNewConnection(handle NetworkFilterHandle) NetworkFilterDataStatus {
	//       sched := handle.GetScheduler()
	//       go func() {
	//           // do some background work
	//           sched.Schedule(func() {
	//               // runs on the worker thread; safe to call handle.* here
	//               handle.ContinueReading()
	//           })
	//       }()
	//       return NetworkFilterDataStatusStopIteration
	//   }
	GetScheduler() Scheduler

	// GetWorkerIndex returns the worker thread index assigned to this filter. Useful for
	// partitioning per-worker resources.
	GetWorkerIndex() uint32
}
