//go:generate mockgen -source=listener.go -destination=mocks/mock_listener.go -package=mocks
package shared

// Listener filter SDK surface for dynamic modules.
//
// This mirrors the Rust SDK's `listener` module. A listener filter runs BEFORE a Connection
// object exists for the accepted socket: it can peek at incoming bytes to detect protocols (TLS,
// PROXY-protocol, JA3/JA4, etc.), set socket-derived stream-info fields, modify the remote/local
// address, or close the socket outright.
//
// A module exposes a listener filter by implementing ListenerFilterConfigFactory and registering
// it from an init() function via sdk.RegisterListenerFilterConfigFactories. Envoy creates a
// ListenerFilter for each accepted connection.
//
// All event hooks on a single ListenerFilter are called on the worker thread that handled the
// accept. Methods on ListenerFilterHandle MUST only be called from that thread, either inside an
// OnXxx callback or from a function scheduled via the Scheduler returned by
// ListenerFilterHandle.GetScheduler.

// ListenerFilterStatus is the status returned by OnAccept and OnData. It corresponds to
// envoy_dynamic_module_type_on_listener_filter_status / Envoy's `Network::FilterStatus`.
type ListenerFilterStatus uint32

const (
	// ListenerFilterStatusContinue allows further filters in the chain to run.
	ListenerFilterStatusContinue ListenerFilterStatus = iota
	// ListenerFilterStatusStopIteration stops further filters from running. The filter is
	// expected to either complete (call ContinueFilterChain), close the socket, or wait for
	// more data / async work.
	ListenerFilterStatusStopIteration
	// ListenerFilterStatusDefault equals ListenerFilterStatusContinue and is what
	// EmptyListenerFilter returns.
	ListenerFilterStatusDefault ListenerFilterStatus = ListenerFilterStatusContinue
)

// AddressType describes the kind of socket address. It corresponds to
// envoy_dynamic_module_type_address_type.
type AddressType uint32

const (
	// AddressTypeUnknown — the address type could not be determined.
	AddressTypeUnknown AddressType = iota
	// AddressTypeIP — IPv4 or IPv6 address.
	AddressTypeIP
	// AddressTypePipe — Unix domain socket.
	AddressTypePipe
	// AddressTypeEnvoyInternal — Envoy internal listener address.
	AddressTypeEnvoyInternal
)

// ListenerFilter is the per-connection module-side listener filter object. A new instance is
// created for each accepted connection by ListenerFilterFactory.Create and destroyed when the
// connection is dispatched to the network filter chain or closed.
//
// All event hooks on a given ListenerFilter are called on the same worker thread on which the
// filter was created. Methods on the ListenerFilterHandle passed to each callback MUST only be
// called on that thread.
//
// Embed EmptyListenerFilter to opt out of any callbacks you don't care about.
type ListenerFilter interface {
	// OnAccept is called when a new connection is accepted on the listener, before any data has
	// been read. Returning ListenerFilterStatusStopIteration halts the filter chain until the
	// module calls ListenerFilterHandle.ContinueFilterChain.
	//
	// To request bytes for inspection, the module should set GetMaxReadBytes on the
	// ListenerFilter to a non-zero value; Envoy will then deliver OnData callbacks once at least
	// that many bytes are buffered (or the socket half-closes).
	OnAccept(handle ListenerFilterHandle) ListenerFilterStatus

	// OnData is called when data is available for inspection. The data is peek-based — it stays
	// in the buffer for subsequent listener filters and the eventual network filter chain. Use
	// ListenerFilterHandle.GetBufferChunk to read it; ListenerFilterHandle.DrainBuffer to remove
	// data from the front (which prevents it from being seen by subsequent filters).
	OnData(handle ListenerFilterHandle) ListenerFilterStatus

	// OnClose is called when the socket is closed. Only filters that have stopped filter-chain
	// iteration receive this callback.
	OnClose(handle ListenerFilterHandle)

	// GetMaxReadBytes is queried by Envoy to determine how many bytes to read before delivering
	// OnData. This is called frequently and should be a fast operation. Returning 0 indicates
	// the filter does not need any data.
	GetMaxReadBytes() uint64

	// OnDestroy is called when the listener filter is destroyed. No ListenerFilterHandle is
	// passed because the underlying Envoy filter pointer is no longer valid by the time this
	// hook fires; modules that need handle access during teardown should do their work in
	// OnClose instead.
	OnDestroy()
}

// EmptyListenerFilter is a no-op ListenerFilter that returns Continue/Default for callbacks and
// 0 for GetMaxReadBytes (i.e., it does not request any bytes for inspection).
type EmptyListenerFilter struct{}

func (*EmptyListenerFilter) OnAccept(_ ListenerFilterHandle) ListenerFilterStatus {
	return ListenerFilterStatusDefault
}
func (*EmptyListenerFilter) OnData(_ ListenerFilterHandle) ListenerFilterStatus {
	return ListenerFilterStatusDefault
}
func (*EmptyListenerFilter) OnClose(_ ListenerFilterHandle) {}
func (*EmptyListenerFilter) GetMaxReadBytes() uint64        { return 0 }
func (*EmptyListenerFilter) OnDestroy()                     {}

// ListenerFilterFactory creates per-connection ListenerFilter instances. It holds the parsed
// configuration and is shared across all accepted connections.
//
// Implementations must be safe for concurrent calls: Envoy may call Create from multiple worker
// threads simultaneously.
type ListenerFilterFactory interface {
	// Create creates a ListenerFilter for a new accepted connection. The handle passed in is
	// bound to the new connection's worker thread.
	Create(handle ListenerFilterHandle) ListenerFilter

	// OnDestroy is called when the factory is destroyed (typically on configuration update,
	// after all in-flight connections using this factory have ended).
	OnDestroy()
}

// EmptyListenerFilterFactory is a no-op ListenerFilterFactory that returns an
// EmptyListenerFilter for every Create call.
type EmptyListenerFilterFactory struct{}

func (*EmptyListenerFilterFactory) Create(_ ListenerFilterHandle) ListenerFilter {
	return &EmptyListenerFilter{}
}
func (*EmptyListenerFilterFactory) OnDestroy() {}

// ListenerFilterConfigFactory is the top-level factory the module registers via
// sdk.RegisterListenerFilterConfigFactories. Envoy calls Create exactly once per filter
// configuration to produce a ListenerFilterFactory.
type ListenerFilterConfigFactory interface {
	// Create parses unparsedConfig (typically a serialized proto) and returns a factory.
	// Returning a non-nil error rejects the configuration.
	Create(handle ListenerFilterConfigHandle, unparsedConfig []byte) (ListenerFilterFactory, error)
}

// EmptyListenerFilterConfigFactory is a no-op ListenerFilterConfigFactory.
type EmptyListenerFilterConfigFactory struct{}

func (*EmptyListenerFilterConfigFactory) Create(_ ListenerFilterConfigHandle, _ []byte) (ListenerFilterFactory, error) {
	return &EmptyListenerFilterFactory{}, nil
}

// ListenerFilterConfigHandle is the config-context handle. It is valid for the lifetime of the
// ListenerFilterFactory and is used to define metrics scoped to the filter configuration and to
// schedule events on the main thread.
type ListenerFilterConfigHandle interface {
	// DefineCounter creates a per-config counter. The returned MetricID can be used with
	// ListenerFilterHandle.IncrementCounter on any connection handled by this configuration.
	DefineCounter(name string) (MetricID, MetricsResult)

	// DefineGauge creates a per-config gauge.
	DefineGauge(name string) (MetricID, MetricsResult)

	// DefineHistogram creates a per-config histogram.
	DefineHistogram(name string) (MetricID, MetricsResult)

	// GetScheduler returns a scheduler bound to the main thread.
	GetScheduler() Scheduler
}

// ListenerFilterHandle is the per-connection handle used for all operations on the connection
// from inside the module. Methods on this handle MUST only be called on the worker thread that
// owns the connection.
type ListenerFilterHandle interface {
	// ---- buffer access ----

	// GetBufferChunk returns the current data buffer as a single chunk. Valid only inside the
	// OnData callback. Returns false if no buffer is available.
	//
	// NOTE: The buffer is owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep it.
	GetBufferChunk() (UnsafeEnvoyBuffer, bool)

	// DrainBuffer drops length bytes from the front of the data buffer. The drained bytes will
	// not be visible to subsequent listener filters or to the network filter chain.
	DrainBuffer(length uint64) bool

	// ---- protocol detection setters (TLS inspector pattern) ----

	// SetDetectedTransportProtocol sets the detected transport protocol on the socket
	// (e.g., "tls", "raw_buffer"). Subsequent listener filters and filter-chain matching see
	// this value.
	SetDetectedTransportProtocol(protocol string)

	// SetRequestedServerName sets the SNI (server name) detected by this filter on the socket.
	SetRequestedServerName(name string)

	// SetRequestedApplicationProtocols sets the ALPN protocols detected by this filter on the
	// socket.
	SetRequestedApplicationProtocols(protocols []string)

	// SetJa3Hash sets the JA3 fingerprint hash on the socket.
	SetJa3Hash(hash string)

	// SetJa4Hash sets the JA4 fingerprint hash on the socket.
	SetJa4Hash(hash string)

	// ---- protocol detection getters & SSL info ----

	// GetRequestedServerName returns the SNI from the connection socket (e.g., set by an
	// earlier TLS inspector). Returns false if no SNI is available.
	//
	// NOTE: The buffer is owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep it.
	GetRequestedServerName() (UnsafeEnvoyBuffer, bool)

	// GetDetectedTransportProtocol returns the transport protocol detected by an earlier
	// listener filter (e.g., "tls", "raw_buffer"). Returns false if not detected.
	GetDetectedTransportProtocol() (UnsafeEnvoyBuffer, bool)

	// GetRequestedApplicationProtocols returns the ALPN protocols set on the socket.
	GetRequestedApplicationProtocols() []UnsafeEnvoyBuffer

	// GetJa3Hash returns the JA3 fingerprint hash from the socket. Returns false if not
	// available.
	GetJa3Hash() (UnsafeEnvoyBuffer, bool)

	// GetJa4Hash returns the JA4 fingerprint hash from the socket. Returns false if not
	// available.
	GetJa4Hash() (UnsafeEnvoyBuffer, bool)

	// IsSSL reports whether SSL/TLS connection information is available on the socket.
	IsSSL() bool

	// GetSSLURISans returns the URI Subject Alternative Names from the peer certificate.
	GetSSLURISans() []UnsafeEnvoyBuffer

	// GetSSLDNSSans returns the DNS Subject Alternative Names from the peer certificate.
	GetSSLDNSSans() []UnsafeEnvoyBuffer

	// GetSSLSubject returns the subject of the peer certificate. Returns false if SSL is not
	// available.
	GetSSLSubject() (UnsafeEnvoyBuffer, bool)

	// ---- addresses ----

	// GetRemoteAddress returns the remote (client) address and port. Returns false if not
	// available or not an IP address.
	GetRemoteAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// GetDirectRemoteAddress returns the direct remote address — the peer address before any
	// listener-filter modification (e.g., proxy-protocol parsing).
	GetDirectRemoteAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// GetLocalAddress returns the local address.
	GetLocalAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// GetDirectLocalAddress returns the direct local address — the listener address before any
	// restoration (e.g., original-destination handling).
	GetDirectLocalAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// GetOriginalDst returns the original destination address obtained from the platform
	// (e.g., from SO_ORIGINAL_DST after iptables redirect).
	GetOriginalDst() (UnsafeEnvoyBuffer, uint32, bool)

	// GetAddressType returns the kind of socket address (IP / Pipe / EnvoyInternal / Unknown).
	GetAddressType() AddressType

	// IsLocalAddressRestored reports whether the local address has been restored to a value
	// different from the listener address (e.g., by an original-destination filter).
	IsLocalAddressRestored() bool

	// SetRemoteAddress sets the remote address on the socket — typically called by a
	// proxy-protocol-parsing filter. isIPv6 selects between IPv4 (false) and IPv6 (true).
	// Returns true on success.
	SetRemoteAddress(address string, port uint32, isIPv6 bool) bool

	// RestoreLocalAddress sets the local address on the socket (used by original-destination
	// handling and proxy-protocol). isIPv6 selects between IPv4 (false) and IPv6 (true).
	// Returns true on success.
	RestoreLocalAddress(address string, port uint32, isIPv6 bool) bool

	// ---- filter chain control ----

	// ContinueFilterChain resumes the listener filter chain after a filter previously returned
	// ListenerFilterStatusStopIteration from OnAccept or OnData. If success is false, the
	// connection is closed instead of progressing through the chain.
	ContinueFilterChain(success bool)

	// UseOriginalDst toggles whether the listener should use the original destination address
	// (set by an original-destination filter) for filter-chain matching.
	UseOriginalDst(useOriginalDst bool)

	// CloseSocket closes the socket immediately. If details is non-empty, it is recorded as
	// the connection's stream-info termination reason for logging.
	CloseSocket(details string)

	// WriteToSocket writes data directly to the raw socket. Useful for protocol negotiation at
	// the listener-filter level (e.g., writing SSL-support responses in Postgres or MySQL
	// handshake packets). Returns the number of bytes written, or -1 on failure.
	WriteToSocket(data []byte) int64

	// ---- socket file descriptor & options ----

	// GetSocketFD returns the raw socket file descriptor for advanced socket manipulations.
	// Returns -1 if the socket is not available.
	GetSocketFD() int64

	// SetSocketOptionInt sets an integer-valued socket option directly on the accepted socket
	// via setsockopt(2). Returns true on success.
	SetSocketOptionInt(level, name, value int64) bool

	// SetSocketOptionBytes sets a bytes-valued socket option directly on the accepted socket
	// via setsockopt(2). Returns true on success.
	SetSocketOptionBytes(level, name int64, value []byte) bool

	// GetSocketOptionInt retrieves an integer socket option value directly from the accepted
	// socket via getsockopt(2). This reads the actual value from the socket, including options
	// set by other filters or the system. Returns false if not retrievable.
	GetSocketOptionInt(level, name int64) (int64, bool)

	// GetSocketOptionBytes retrieves a bytes socket option value directly from the accepted
	// socket via getsockopt(2). maxSize bounds the returned slice. Returns false if not
	// retrievable.
	GetSocketOptionBytes(level, name int64, maxSize uint64) ([]byte, bool)

	// ---- filter state & dynamic metadata ----

	// SetFilterState stores a string value under key in the connection's filter state with
	// Connection lifespan.
	SetFilterState(key, value string) bool

	// GetFilterState retrieves a string value previously stored under key.
	//
	// NOTE: The buffer is owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep it.
	GetFilterState(key string) (UnsafeEnvoyBuffer, bool)

	SetDynamicMetadataString(metadataNamespace, key, value string)
	GetDynamicMetadataString(metadataNamespace, key string) (UnsafeEnvoyBuffer, bool)
	SetDynamicMetadataNumber(metadataNamespace, key string, value float64)
	GetDynamicMetadataNumber(metadataNamespace, key string) (float64, bool)

	// ---- stream info ----

	// SetDownstreamTransportFailureReason sets the downstream transport failure reason on the
	// stream info — useful for logging and debugging when the listener filter terminates the
	// connection.
	SetDownstreamTransportFailureReason(reason string)

	// GetConnectionStartTimeMs returns the connection start time in milliseconds since Unix
	// epoch.
	GetConnectionStartTimeMs() uint64

	// MaxReadBytes returns the per-listener-filter maximum number of bytes to read from the
	// socket. This is the same value as ListenerFilter.GetMaxReadBytes returned, queried back
	// from Envoy.
	MaxReadBytes() uint64

	// ---- HTTP callouts ----

	// HttpCallout sends an asynchronous HTTP request against the named cluster. headers must
	// include :method, :path, and host. The result is delivered via the supplied
	// HttpCalloutCallback.
	//
	// HttpCalloutInitResult values:
	//
	//   - HttpCalloutInitSuccess          — the callout was scheduled.
	//   - HttpCalloutInitMissingRequiredHeaders — :method, :path, or host was missing.
	//   - HttpCalloutInitClusterNotFound  — clusterName did not match any cluster.
	//   - HttpCalloutInitCannotCreateRequest   — request could not be created (e.g., no healthy
	//     upstream host in the cluster).
	HttpCallout(clusterName string, headers [][2]string, body []byte, timeoutMs uint64,
		cb HttpCalloutCallback) (HttpCalloutInitResult, uint64)

	// ---- metrics ----

	IncrementCounter(id MetricID, value uint64) MetricsResult
	SetGauge(id MetricID, value uint64) MetricsResult
	IncrementGauge(id MetricID, value uint64) MetricsResult
	DecrementGauge(id MetricID, value uint64) MetricsResult
	RecordHistogramValue(id MetricID, value uint64) MetricsResult

	// ---- scheduling / misc ----

	// GetScheduler returns a scheduler bound to this filter's worker thread. Tasks scheduled
	// on it will run on this filter's worker, allowing background work to safely re-enter
	// ListenerFilterHandle methods.
	//
	// Tasks are delivered IF the filter is still alive when the task fires; otherwise they are
	// dropped silently.
	GetScheduler() Scheduler

	// GetWorkerIndex returns the worker thread index assigned to this filter.
	GetWorkerIndex() uint32
}
