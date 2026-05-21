//go:generate mockgen -source=types.go -destination=mocks/mock_types.go -package=mocks
package shared

import (
	"strings"
	"unsafe"
)

// Cross-surface types and interfaces shared by multiple SDK surfaces (HTTP filter, network
// filter, listener filter, access logger, etc.). Surface-specific types live in their own
// files (http.go for HTTP, network.go for network, and so on).

// UnsafeEnvoyBuffer is a struct that represents a buffer of data from Envoy.
// It contains a pointer to the data and its length. The memory of the data is managed by Envoy.
type UnsafeEnvoyBuffer struct {
	// Pointer to the start of the buffer data.
	Ptr *byte
	// Length of the buffer data in bytes.
	Len uint64
}

func (b UnsafeEnvoyBuffer) ToUnsafeBytes() []byte {
	if b.Ptr == nil || b.Len == 0 {
		return nil
	}
	// Use unsafe to create a byte slice that points to the buffer data without copying.
	return unsafe.Slice(b.Ptr, b.Len)
}

// ToBytes converts the UnsafeEnvoyBuffer to a byte slice. It creates a copy of the data in Go memory.
func (b UnsafeEnvoyBuffer) ToBytes() []byte {
	if b.Ptr == nil || b.Len == 0 {
		return nil
	}
	// Create a byte slice that copys the data from the buffer.
	owned := make([]byte, b.Len)
	// Use unsafe to copy the data from the buffer to the byte slice.
	src := unsafe.Slice(b.Ptr, b.Len)
	copy(owned, src)
	return owned
}

func (b UnsafeEnvoyBuffer) ToUnsafeString() string {
	if b.Ptr == nil || b.Len == 0 {
		return ""
	}
	// Use unsafe to create a string that points to the buffer data without copying.
	return unsafe.String(b.Ptr, b.Len)
}

func (b UnsafeEnvoyBuffer) ToString() string {
	if b.Ptr == nil || b.Len == 0 {
		return ""
	}
	return strings.Clone(b.ToUnsafeString())
}

// AttributeID identifies an attribute of the current request, response, connection, or upstream
// host that can be retrieved via Get{Attribute,...} family of methods. Corresponds to
// envoy_dynamic_module_type_attribute_id.
type AttributeID uint32

const (
	// request.path
	AttributeIDRequestPath AttributeID = iota
	// request.url_path
	AttributeIDRequestUrlPath
	// request.host
	AttributeIDRequestHost
	// request.scheme
	AttributeIDRequestScheme
	// request.method
	AttributeIDRequestMethod
	// request.headers
	AttributeIDRequestHeaders
	// request.referer
	AttributeIDRequestReferer
	// request.useragent
	AttributeIDRequestUserAgent
	// request.time
	AttributeIDRequestTime
	// request.id
	AttributeIDRequestId
	// request.protocol
	AttributeIDRequestProtocol
	// request.query
	AttributeIDRequestQuery
	// request.duration
	AttributeIDRequestDuration
	// request.size
	AttributeIDRequestSize
	// request.total_size
	AttributeIDRequestTotalSize
	// response.code
	AttributeIDResponseCode
	// response.code_details
	AttributeIDResponseCodeDetails
	// response.flags
	AttributeIDResponseFlags
	// response.grpc_status
	AttributeIDResponseGrpcStatus
	// response.headers
	AttributeIDResponseHeaders
	// response.trailers
	AttributeIDResponseTrailers
	// response.size
	AttributeIDResponseSize
	// response.total_size
	AttributeIDResponseTotalSize
	// response.backend_latency
	AttributeIDResponseBackendLatency
	// source.address
	AttributeIDSourceAddress
	// source.port
	AttributeIDSourcePort
	// destination.address
	AttributeIDDestinationAddress
	// destination.port
	AttributeIDDestinationPort
	// connection.id
	AttributeIDConnectionId
	// connection.mtls
	AttributeIDConnectionMTLS
	// connection.requested_server_name
	AttributeIDConnectionRequestedServerName
	// connection.tls_version
	AttributeIDConnectionTLSVersion
	// connection.subject_local_certificate
	AttributeIDConnectionSubjectLocalCertificate
	// connection.subject_peer_certificate
	AttributeIDConnectionSubjectPeerCertificate
	// connection.dns_san_local_certificate
	AttributeIDConnectionDNSSanLocalCertificate
	// connection.dns_san_peer_certificate
	AttributeIDConnectionDNSSanPeerCertificate
	// connection.uri_san_local_certificate
	AttributeIDConnectionURISanLocalCertificate
	// connection.uri_san_peer_certificate
	AttributeIDConnectionURISanPeerCertificate
	// connection.sha256_peer_certificate_digest
	AttributeIDConnectionSha256PeerCertificateDigest
	// connection.transport_failure_reason
	AttributeIDConnectionTransportFailureReason
	// connection.termination_details
	AttributeIDConnectionTerminationDetails
	// upstream.address
	AttributeIDUpstreamAddress
	// upstream.port
	AttributeIDUpstreamPort
	// upstream.tls_version
	AttributeIDUpstreamTLSVersion
	// upstream.subject_local_certificate
	AttributeIDUpstreamSubjectLocalCertificate
	// upstream.subject_peer_certificate
	AttributeIDUpstreamSubjectPeerCertificate
	// upstream.dns_san_local_certificate
	AttributeIDUpstreamDNSSanLocalCertificate
	// upstream.dns_san_peer_certificate
	AttributeIDUpstreamDNSSanPeerCertificate
	// upstream.uri_san_local_certificate
	AttributeIDUpstreamURISanLocalCertificate
	// upstream.uri_san_peer_certificate
	AttributeIDUpstreamURISanPeerCertificate
	// upstream.sha256_peer_certificate_digest
	AttributeIDUpstreamSha256PeerCertificateDigest
	// upstream.local_address
	AttributeIDUpstreamLocalAddress
	// upstream.transport_failure_reason
	AttributeIDUpstreamTransportFailureReason
	// upstream.request_attempt_count
	AttributeIDUpstreamRequestAttemptCount
	// upstream.cx_pool_ready_duration
	AttributeIDUpstreamCxPoolReadyDuration
	// upstream.locality
	AttributeIDUpstreamLocality
	// xds.node
	AttributeIDXdsNode
	// xds.cluster_name
	AttributeIDXdsClusterName
	// xds.cluster_metadata
	AttributeIDXdsClusterMetadata
	// xds.listener_direction
	AttributeIDXdsListenerDirection
	// xds.listener_metadata
	AttributeIDXdsListenerMetadata
	// xds.route_name
	AttributeIDXdsRouteName
	// xds.route_metadata
	AttributeIDXdsRouteMetadata
	// xds.virtual_host_name
	AttributeIDXdsVirtualHostName
	// xds.virtual_host_metadata
	AttributeIDXdsVirtualHostMetadata
	// xds.upstream_host_metadata
	AttributeIDXdsUpstreamHostMetadata
	// xds.filter_chain_name
	AttributeIDXdsFilterChainName
	// health_check
	AttributeIDHealthCheck
)

// LogLevel is the log level for messages logged via the host environment's logging mechanism.
type LogLevel uint32

const (
	LogLevelTrace LogLevel = iota
	LogLevelDebug
	LogLevelInfo
	LogLevelWarn
	LogLevelError
	LogLevelCritical
	LogLevelOff
)

// HttpCalloutInitResult is the result of initializing an HTTP callout or stream.
type HttpCalloutInitResult uint32

const (
	HttpCalloutInitSuccess HttpCalloutInitResult = iota
	HttpCalloutInitMissingRequiredHeaders
	HttpCalloutInitClusterNotFound
	HttpCalloutInitDuplicateCalloutId
	HttpCalloutInitCannotCreateRequest
)

// HttpCalloutResult is the result of a completed HTTP callout (delivered via
// HttpCalloutCallback.OnHttpCalloutDone).
type HttpCalloutResult uint32

const (
	HttpCalloutSuccess HttpCalloutResult = iota
	HttpCalloutReset
	HttpCalloutExceedResponseBufferLimit
)

// HttpCalloutCallback is the callback interface invoked when an HTTP callout completes.
type HttpCalloutCallback interface {
	OnHttpCalloutDone(calloutID uint64, result HttpCalloutResult,
		headers [][2]UnsafeEnvoyBuffer, body []UnsafeEnvoyBuffer)
}

// HttpStreamResetReason is the reason that an HTTP stream (started via StartHttpStream)
// was reset.
type HttpStreamResetReason uint32

const (
	HttpStreamResetReasonConnectionFailure HttpStreamResetReason = iota
	HttpStreamResetReasonConnectionTermination
	HttpStreamResetReasonLocalReset
	HttpStreamResetReasonLocalRefusedStreamReset
	HttpStreamResetReasonOverflow
	HttpStreamResetReasonRemoteReset
	HttpStreamResetReasonRemoteRefusedStreamReset
	HttpStreamResetReasonProtocolError
)

// HttpStreamCallback is the callback interface invoked for events on an HTTP stream started
// via StartHttpStream.
type HttpStreamCallback interface {
	OnHttpStreamHeaders(streamID uint64, headers [][2]UnsafeEnvoyBuffer, endStream bool)
	OnHttpStreamData(streamID uint64, body []UnsafeEnvoyBuffer, endStream bool)
	OnHttpStreamTrailers(streamID uint64, trailers [][2]UnsafeEnvoyBuffer)
	OnHttpStreamComplete(streamID uint64)
	OnHttpStreamReset(streamID uint64, reason HttpStreamResetReason)
}

// Scheduler is the interface that provides scheduling capabilities for asynchronous operations.
// This allow the plugins run tasks in another thread and continue the processing later at the
// thread where the stream plugin is being processed.
type Scheduler interface {
	// Schedule schedules a function to be executed asynchronously in the thread where the stream
	// plugin is being processed.
	//
	// NOTE: The function may be ignored if the related plugin processing is completed.
	Schedule(func())
}

// SocketOptionState represents the socket state at which an option should be applied.
// This corresponds to envoy_dynamic_module_type_socket_option_state in the dynamic module ABI.
type SocketOptionState uint32

const (
	// SocketOptionStatePrebind applies the option before the socket is bound.
	SocketOptionStatePrebind SocketOptionState = iota
	// SocketOptionStateBound applies the option after the socket is bound.
	SocketOptionStateBound
	// SocketOptionStateListening applies the option after the socket starts listening.
	SocketOptionStateListening
)

// SocketDirection represents whether the socket option should be applied to the upstream
// (outgoing to backend) or downstream (incoming from client) connection.
// This corresponds to envoy_dynamic_module_type_socket_direction in the dynamic module ABI.
type SocketDirection uint32

const (
	// SocketDirectionUpstream applies the option to the upstream (outgoing) connection.
	SocketDirectionUpstream SocketDirection = iota
	// SocketDirectionDownstream applies the option to the downstream (incoming) connection.
	SocketDirectionDownstream
)

// ClusterHostCounts carries the host counts returned by HttpFilterHandle.GetClusterHostCounts.
type ClusterHostCounts struct {
	// Total is the total number of hosts in the priority set.
	Total uint64
	// Healthy is the number of hosts in the HEALTHY state.
	Healthy uint64
	// Degraded is the number of hosts in the DEGRADED state.
	Degraded uint64
}

// MetricID is an opaque identifier for a metric defined via Define{Counter,Gauge,Histogram}.
type MetricID uint64

// MetricsResult is the result of a metric definition or update operation.
type MetricsResult uint32

const (
	MetricsSuccess MetricsResult = iota
	MetricsNotFound
	MetricsInvalidTags
	MetricsFrozen
)

// HttpHeaderType identifies which HTTP header map to access. It corresponds to
// envoy_dynamic_module_type_http_header_type. The values match the ABI's enum order:
// RequestHeader, RequestTrailer, ResponseHeader, ResponseTrailer.
type HttpHeaderType uint32

const (
	HttpHeaderTypeRequestHeader   HttpHeaderType = 0
	HttpHeaderTypeRequestTrailer  HttpHeaderType = 1
	HttpHeaderTypeResponseHeader  HttpHeaderType = 2
	HttpHeaderTypeResponseTrailer HttpHeaderType = 3
)
