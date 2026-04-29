//go:generate mockgen -source=access_log.go -destination=mocks/mock_access_log.go -package=mocks
package shared

// Access logger SDK surface for dynamic modules.
//
// This mirrors the Rust SDK's `access_log` module. A module that exposes an access logger
// implements AccessLoggerConfigFactory and registers it from an init() function via
// sdk.RegisterAccessLoggerConfigFactories.
//
// Lifecycle: Envoy calls AccessLoggerConfigFactory.Create exactly once per access-log
// configuration to obtain an AccessLoggerFactory. The factory is then asked to Create per-thread
// AccessLogger instances; each AccessLogger receives Log calls for every log event on the
// thread it owns. On shutdown, Flush is called once before OnDestroy.

// AccessLogType identifies the kind of access-log event being delivered to AccessLogger.Log. It
// corresponds to envoy_dynamic_module_type_access_log_type / envoy::data::accesslog::v3::AccessLogType.
type AccessLogType uint32

const (
	AccessLogTypeNotSet                                AccessLogType = 0
	AccessLogTypeTcpUpstreamConnected                  AccessLogType = 1
	AccessLogTypeTcpPeriodic                           AccessLogType = 2
	AccessLogTypeTcpConnectionEnd                      AccessLogType = 3
	AccessLogTypeDownstreamStart                       AccessLogType = 4
	AccessLogTypeDownstreamPeriodic                    AccessLogType = 5
	AccessLogTypeDownstreamEnd                         AccessLogType = 6
	AccessLogTypeUpstreamPoolReady                     AccessLogType = 7
	AccessLogTypeUpstreamPeriodic                      AccessLogType = 8
	AccessLogTypeUpstreamEnd                           AccessLogType = 9
	AccessLogTypeDownstreamTunnelSuccessfullyEstablished AccessLogType = 10
	AccessLogTypeUdpTunnelUpstreamConnected            AccessLogType = 11
	AccessLogTypeUdpPeriodic                           AccessLogType = 12
	AccessLogTypeUdpSessionEnd                         AccessLogType = 13
)

// ResponseFlag corresponds to envoy_dynamic_module_type_response_flag and Envoy's
// CoreResponseFlag enum. Use AccessLogContext.HasResponseFlag to test for individual flags or
// GetResponseFlags to retrieve the bitmask.
type ResponseFlag uint32

const (
	ResponseFlagFailedLocalHealthCheck         ResponseFlag = 0
	ResponseFlagNoHealthyUpstream              ResponseFlag = 1
	ResponseFlagUpstreamRequestTimeout         ResponseFlag = 2
	ResponseFlagLocalReset                     ResponseFlag = 3
	ResponseFlagUpstreamRemoteReset            ResponseFlag = 4
	ResponseFlagUpstreamConnectionFailure      ResponseFlag = 5
	ResponseFlagUpstreamConnectionTermination  ResponseFlag = 6
	ResponseFlagUpstreamOverflow               ResponseFlag = 7
	ResponseFlagNoRouteFound                   ResponseFlag = 8
	ResponseFlagDelayInjected                  ResponseFlag = 9
	ResponseFlagFaultInjected                  ResponseFlag = 10
	ResponseFlagRateLimited                    ResponseFlag = 11
	ResponseFlagUnauthorizedExternalService    ResponseFlag = 12
	ResponseFlagRateLimitServiceError          ResponseFlag = 13
	ResponseFlagDownstreamConnectionTermination ResponseFlag = 14
	ResponseFlagUpstreamRetryLimitExceeded     ResponseFlag = 15
	ResponseFlagStreamIdleTimeout              ResponseFlag = 16
	ResponseFlagInvalidEnvoyRequestHeaders     ResponseFlag = 17
	ResponseFlagDownstreamProtocolError        ResponseFlag = 18
	ResponseFlagUpstreamMaxStreamDurationReached ResponseFlag = 19
	ResponseFlagResponseFromCacheFilter        ResponseFlag = 20
	ResponseFlagNoFilterConfigFound            ResponseFlag = 21
	ResponseFlagDurationTimeout                ResponseFlag = 22
	ResponseFlagUpstreamProtocolError          ResponseFlag = 23
	ResponseFlagNoClusterFound                 ResponseFlag = 24
	ResponseFlagOverloadManager                ResponseFlag = 25
	ResponseFlagDnsResolutionFailed            ResponseFlag = 26
	ResponseFlagDropOverLoad                   ResponseFlag = 27
	ResponseFlagDownstreamRemoteReset          ResponseFlag = 28
	ResponseFlagUnconditionalDropOverload      ResponseFlag = 29
)

// AccessLogTimingInfo carries per-stream timing data. All durations are in nanoseconds; -1
// indicates the timing is not available.
type AccessLogTimingInfo struct {
	// StartTimeUnixNs is the request start time as a Unix timestamp in nanoseconds.
	StartTimeUnixNs int64
	// RequestCompleteDurationNs is the duration from start to request complete.
	RequestCompleteDurationNs int64
	FirstUpstreamTxByteSentNs       int64
	LastUpstreamTxByteSentNs        int64
	FirstUpstreamRxByteReceivedNs   int64
	LastUpstreamRxByteReceivedNs    int64
	FirstDownstreamTxByteSentNs     int64
	LastDownstreamTxByteSentNs      int64
}

// AccessLogBytesInfo carries per-stream byte-count totals. All values are 0 if not available.
type AccessLogBytesInfo struct {
	// BytesReceived is the total bytes received from downstream.
	BytesReceived uint64
	// BytesSent is the total bytes sent to downstream.
	BytesSent uint64
	// WireBytesReceived is the wire bytes received (including TLS overhead).
	WireBytesReceived uint64
	// WireBytesSent is the wire bytes sent (including TLS overhead).
	WireBytesSent uint64
}

// AccessLogger is the per-thread (or shared) module-side access logger. The runtime calls Log
// for each log event on the thread it owns; Flush is called once during shutdown before
// OnDestroy.
type AccessLogger interface {
	// Log is called when a log event occurs. The handle is valid only for the duration of this
	// callback; do not retain it.
	Log(handle AccessLogContext, logType AccessLogType)

	// Flush is called once during shutdown before OnDestroy, giving the module a chance to
	// flush any buffered log entries. Implementations that don't buffer can leave this empty.
	Flush()

	// OnDestroy is called when the logger instance is destroyed.
	OnDestroy()
}

// EmptyAccessLogger is a no-op AccessLogger that does nothing.
type EmptyAccessLogger struct{}

func (*EmptyAccessLogger) Log(_ AccessLogContext, _ AccessLogType) {}
func (*EmptyAccessLogger) Flush()                                  {}
func (*EmptyAccessLogger) OnDestroy()                              {}

// AccessLoggerFactory creates per-thread (or shared) AccessLogger instances. Returning the same
// AccessLogger from multiple Create calls is allowed if the implementation is thread-safe.
type AccessLoggerFactory interface {
	// Create creates an AccessLogger.
	Create() AccessLogger

	// OnDestroy is called when the factory is destroyed.
	OnDestroy()
}

// EmptyAccessLoggerFactory is a no-op AccessLoggerFactory.
type EmptyAccessLoggerFactory struct{}

func (*EmptyAccessLoggerFactory) Create() AccessLogger { return &EmptyAccessLogger{} }
func (*EmptyAccessLoggerFactory) OnDestroy()           {}

// AccessLoggerConfigFactory is the top-level factory the module registers via
// sdk.RegisterAccessLoggerConfigFactories.
type AccessLoggerConfigFactory interface {
	// Create parses unparsedConfig and returns an AccessLoggerFactory.
	Create(handle AccessLoggerConfigHandle, unparsedConfig []byte) (AccessLoggerFactory, error)
}

// EmptyAccessLoggerConfigFactory is a no-op AccessLoggerConfigFactory.
type EmptyAccessLoggerConfigFactory struct{}

func (*EmptyAccessLoggerConfigFactory) Create(_ AccessLoggerConfigHandle, _ []byte) (AccessLoggerFactory, error) {
	return &EmptyAccessLoggerFactory{}, nil
}

// AccessLoggerConfigHandle is the config-context handle. Note that for access loggers, metrics
// methods (Increment* / Set* / RecordHistogramValue) are also available on the config handle —
// they are scoped to the configuration rather than to a per-stream context, since loggers are
// not per-stream.
type AccessLoggerConfigHandle interface {
	// DefineCounter creates a per-config counter.
	DefineCounter(name string) (MetricID, MetricsResult)

	// DefineGauge creates a per-config gauge.
	DefineGauge(name string) (MetricID, MetricsResult)

	// DefineHistogram creates a per-config histogram.
	DefineHistogram(name string) (MetricID, MetricsResult)

	// IncrementCounter increments a counter previously defined via DefineCounter.
	IncrementCounter(id MetricID, value uint64) MetricsResult

	// SetGauge sets a gauge previously defined via DefineGauge.
	SetGauge(id MetricID, value uint64) MetricsResult

	// IncrementGauge adds value to a gauge previously defined via DefineGauge.
	IncrementGauge(id MetricID, value uint64) MetricsResult

	// DecrementGauge subtracts value from a gauge previously defined via DefineGauge.
	DecrementGauge(id MetricID, value uint64) MetricsResult

	// RecordHistogramValue records value in a histogram previously defined via DefineHistogram.
	RecordHistogramValue(id MetricID, value uint64) MetricsResult
}

// AccessLogContext is the per-event handle passed to AccessLogger.Log. It is valid only for the
// duration of the Log call; do not retain it. All getters return Envoy-owned memory whose
// validity ends with the callback unless otherwise noted.
//
// The interface is large because access logs need to surface every per-stream attribute. For
// most attributes, prefer GetAttributeString / GetAttributeNumber / GetAttributeBool with the
// AttributeID enum from types.go — those provide a unified accessor that mirrors the HTTP filter
// attribute API. The named getters below are kept for ABI parity but most are deprecated in
// favor of the attribute accessors.
type AccessLogContext interface {
	// ---- headers ----

	// GetHeadersSize returns the number of headers in the specified header map. Supported types
	// are RequestHeader, ResponseHeader, and ResponseTrailer.
	GetHeadersSize(headerType HttpHeaderType) uint64

	// GetHeaders returns all headers from the specified header map.
	GetHeaders(headerType HttpHeaderType) [][2]UnsafeEnvoyBuffer

	// GetHeaderValue returns a header value by key. index selects among multi-value headers
	// (0 = first). The third return value is the total count of values for the key (0 when not
	// found).
	GetHeaderValue(headerType HttpHeaderType, key string, index uint64) (UnsafeEnvoyBuffer, uint64, bool)

	// ---- attribute accessors (preferred) ----

	// GetAttributeString returns a string attribute value. See AttributeID in types.go.
	GetAttributeString(id AttributeID) (UnsafeEnvoyBuffer, bool)

	// GetAttributeNumber returns an integer attribute value.
	GetAttributeNumber(id AttributeID) (uint64, bool)

	// GetAttributeBool returns a boolean attribute value.
	GetAttributeBool(id AttributeID) (bool, bool)

	// ---- response flags / timing / bytes ----

	// HasResponseFlag returns true if the given response flag is set on the stream.
	HasResponseFlag(flag ResponseFlag) bool

	// GetResponseFlags returns all response flags as a bitmask (bit i set ⇔ ResponseFlag(i)).
	GetResponseFlags() uint64

	// GetTimingInfo returns the per-stream timing struct. Individual fields are -1 if
	// unavailable.
	GetTimingInfo() AccessLogTimingInfo

	// GetBytesInfo returns the per-stream byte counts.
	GetBytesInfo() AccessLogBytesInfo

	// ---- addresses ----

	GetDownstreamRemoteAddress() (UnsafeEnvoyBuffer, uint32, bool)
	GetDownstreamLocalAddress() (UnsafeEnvoyBuffer, uint32, bool)
	GetDownstreamDirectRemoteAddress() (UnsafeEnvoyBuffer, uint32, bool)
	GetDownstreamDirectLocalAddress() (UnsafeEnvoyBuffer, uint32, bool)
	GetUpstreamRemoteAddress() (UnsafeEnvoyBuffer, uint32, bool)
	GetUpstreamLocalAddress() (UnsafeEnvoyBuffer, uint32, bool)

	// ---- upstream info ----

	GetUpstreamCluster() (UnsafeEnvoyBuffer, bool)
	GetUpstreamHost() (UnsafeEnvoyBuffer, bool)
	GetUpstreamConnectionID() uint64

	// GetUpstreamTLSCipher returns the upstream TLS cipher suite. The returned buffer uses
	// thread-local storage and is valid until the next call to this function or
	// GetDownstreamTLSCipher on the same thread.
	GetUpstreamTLSCipher() (UnsafeEnvoyBuffer, bool)
	GetUpstreamTLSSessionID() (UnsafeEnvoyBuffer, bool)
	GetUpstreamPeerIssuer() (UnsafeEnvoyBuffer, bool)

	// GetUpstreamPeerCertValidityStart returns the validity-start (notBefore) of the upstream
	// peer certificate as epoch seconds, or 0 if not available.
	GetUpstreamPeerCertValidityStart() int64

	// GetUpstreamPeerCertValidityEnd returns the validity-end (notAfter) of the upstream peer
	// certificate as epoch seconds, or 0 if not available.
	GetUpstreamPeerCertValidityEnd() int64

	GetUpstreamPeerURISans() []UnsafeEnvoyBuffer
	GetUpstreamLocalURISans() []UnsafeEnvoyBuffer
	GetUpstreamPeerDNSSans() []UnsafeEnvoyBuffer
	GetUpstreamLocalDNSSans() []UnsafeEnvoyBuffer

	// ---- downstream connection / TLS info ----

	// GetDownstreamTLSCipher returns the downstream TLS cipher suite. The returned buffer uses
	// thread-local storage and is valid until the next call to this function or
	// GetUpstreamTLSCipher on the same thread.
	GetDownstreamTLSCipher() (UnsafeEnvoyBuffer, bool)
	GetDownstreamTLSSessionID() (UnsafeEnvoyBuffer, bool)
	GetDownstreamPeerIssuer() (UnsafeEnvoyBuffer, bool)
	GetDownstreamPeerSerial() (UnsafeEnvoyBuffer, bool)
	GetDownstreamPeerFingerprint1() (UnsafeEnvoyBuffer, bool)

	// GetDownstreamPeerCertPresented reports whether a peer certificate was presented.
	GetDownstreamPeerCertPresented() bool

	// GetDownstreamPeerCertValidated reports whether the peer certificate was validated.
	GetDownstreamPeerCertValidated() bool

	GetDownstreamPeerCertValidityStart() int64
	GetDownstreamPeerCertValidityEnd() int64

	GetDownstreamPeerURISans() []UnsafeEnvoyBuffer
	GetDownstreamLocalURISans() []UnsafeEnvoyBuffer
	GetDownstreamPeerDNSSans() []UnsafeEnvoyBuffer
	GetDownstreamLocalDNSSans() []UnsafeEnvoyBuffer

	// ---- metadata / filter state / tracing ----

	// GetDynamicMetadata returns a value from dynamic metadata by filter namespace and key
	// path. The path may be nested with dots. Complex (non-string) values are returned as JSON.
	GetDynamicMetadata(filterName, path string) (UnsafeEnvoyBuffer, bool)

	// GetFilterState returns the serialized representation of a filter-state value by key.
	GetFilterState(key string) (UnsafeEnvoyBuffer, bool)

	GetLocalReplyBody() (UnsafeEnvoyBuffer, bool)

	GetTraceID() (UnsafeEnvoyBuffer, bool)
	GetSpanID() (UnsafeEnvoyBuffer, bool)
	IsTraceSampled() bool

	// ---- additional stream info ----

	GetJa3Hash() (UnsafeEnvoyBuffer, bool)
	GetJa4Hash() (UnsafeEnvoyBuffer, bool)

	GetRequestHeadersBytes() uint64
	GetResponseHeadersBytes() uint64
	GetResponseTrailersBytes() uint64
	GetUpstreamProtocol() (UnsafeEnvoyBuffer, bool)

	// GetUpstreamPoolReadyDurationNs returns the time from when the upstream request was
	// created to when the connection pool became ready, in nanoseconds. Returns -1 if not
	// available.
	GetUpstreamPoolReadyDurationNs() int64

	// GetWorkerIndex returns the worker thread index.
	GetWorkerIndex() uint32
}
