//go:generate mockgen -source=base.go -destination=mocks/mock_base.go -package=mocks
package shared

// BodyBuffer is an interface that provides access to the request and response body.
// This should be implemented by the SDK or runtime.
type BodyBuffer interface {
	// GetChunks retrieves the body content as a list of byte slices.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetChunks() [][]byte

	// GetSize retrieves the total size of the body buffer.
	GetSize() uint64

	// Drain removes the specified number of bytes from the beginning of the body buffer.
	// @Param numBytes the number of bytes to drain.
	Drain(numBytes uint64)

	// Append adds the specified bytes to the end of the body buffer.
	// @Param data the bytes to append.
	Append(data []byte)
}

// HeaderMap is an interface that provides access to the request and response headers.
// This should be implemented by the SDK or runtime.
type HeaderMap interface {
	// Get retrieves the header values for a given key. If the key does not exist,
	// nil will be returned.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	Get(key string) []string

	// GetOne retrieves a single header value for a given key.
	// If there are multiple values for the key, the first one will be returned.
	// If the key does not exist, an empty string will be returned.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetOne(key string) string

	// GetAll retrieves all header values. You should not mutate the returned map
	// directly.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetAll() [][2]string

	// Set sets the header value for a given key.
	Set(key string, value string)

	// Add adds a single header value for a given key.
	Add(key string, value string)

	// Remove removes the header values for a given key.
	Remove(key string)
}

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
	AttributeIDConnectionMtls
	// connection.requested_server_name
	AttributeIDConnectionRequestedServerName
	// connection.tls_version
	AttributeIDConnectionTlsVersion
	// connection.subject_local_certificate
	AttributeIDConnectionSubjectLocalCertificate
	// connection.subject_peer_certificate
	AttributeIDConnectionSubjectPeerCertificate
	// connection.dns_san_local_certificate
	AttributeIDConnectionDnsSanLocalCertificate
	// connection.dns_san_peer_certificate
	AttributeIDConnectionDnsSanPeerCertificate
	// connection.uri_san_local_certificate
	AttributeIDConnectionUriSanLocalCertificate
	// connection.uri_san_peer_certificate
	AttributeIDConnectionUriSanPeerCertificate
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
	AttributeIDUpstreamTlsVersion
	// upstream.subject_local_certificate
	AttributeIDUpstreamSubjectLocalCertificate
	// upstream.subject_peer_certificate
	AttributeIDUpstreamSubjectPeerCertificate
	// upstream.dns_san_local_certificate
	AttributeIDUpstreamDnsSanLocalCertificate
	// upstream.dns_san_peer_certificate
	AttributeIDUpstreamDnsSanPeerCertificate
	// upstream.uri_san_local_certificate
	AttributeIDUpstreamUriSanLocalCertificate
	// upstream.uri_san_peer_certificate
	AttributeIDUpstreamUriSanPeerCertificate
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
)

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

type HttpCalloutInitResult uint32

const (
	HttpCalloutInitSuccess HttpCalloutInitResult = iota
	HttpCalloutInitMissingRequiredHeaders
	HttpCalloutInitClusterNotFound
	HttpCalloutInitDuplicateCalloutId
	HttpCalloutInitCannotCreateRequest
)

type HttpCalloutResult uint32

const (
	HttpCalloutSuccess HttpCalloutResult = iota
	HttpCalloutReset
	HttpCalloutExceedResponseBufferLimit
)

type MetadataSourceType uint32

const (
	MetadataSourceTypeDynamic MetadataSourceType = iota
	MetadataSourceTypeRoute
	MetadataSourceTypeCluster
	MetadataSourceTypeHost
	MetadataSourceTypeHostLocality
)

type HttpCalloutCallback interface {
	OnHttpCalloutDone(calloutID uint64, result HttpCalloutResult,
		headers [][2]string, body [][]byte)
}

type HttpStreamResetReason uint32

const (
	HttpStreamResetReasonConnectionFailure = iota
	HttpStreamResetReasonConnectionTermination
	HttpStreamResetReasonLocalReset
	HttpStreamResetReasonLocalRefusedStreamReset
	HttpStreamResetReasonOverflow
	HttpStreamResetReasonRemoteReset
	HttpStreamResetReasonRemoteRefusedStreamReset
	HttpStreamResetReasonProtocolError
)

type HttpStreamCallback interface {
	OnHttpStreamHeaders(streamID uint64, headers [][2]string, endStream bool)
	OnHttpStreamData(streamID uint64, body [][]byte, endStream bool)
	OnHttpStreamTrailers(streamID uint64, trailers [][2]string)
	OnHttpStreamComplete(streamID uint64)
	OnHttpStreamReset(streamID uint64, reason HttpStreamResetReason)
}

// Scheduler is the interface that provides scheduling capabilities for asynchronous operations.
// This allow the plugins run tasks in another thread and continue the processing later at the
// thread where the stream plugin is being processed.
type Scheduler interface {
	// Schedule schedules a function to be executed asynchronously in the thread where the stream
	// plugin is being processed.
	// @Param func the function to be executed.
	// NOTE: This function may be ignored if the related plugin processing is completed.
	Schedule(func())
}

type DownstreamWatermarkCallbacks interface {
	OnAboveWriteBufferHighWatermark()
	OnBelowWriteBufferLowWatermark()
}

// HttpFilterHandle is the interface that provides access to the plugin's context and configuration.
// This should be implemented by the SDK or runtime.
type HttpFilterHandle interface {
	// GetMetadataString retrieves the dynamic metadata string value of the stream.
	// @Param source the metadata source type.
	// @Param metadataNamespace the metadata namespace.
	// @Param key the metadata key.
	// @Return the metadata value if found, otherwise nil.
	GetMetadataString(source MetadataSourceType, metadataNamespace, key string) (string, bool)

	// GetMetadataNumber retrieves the dynamic metadata number value of the stream.
	// @Param source the metadata source type.
	// @Param metadataNamespace the metadata namespace.
	// @Param key the metadata key.
	// @Return the metadata value if found, otherwise nil.
	GetMetadataNumber(source MetadataSourceType, metadataNamespace, key string) (float64, bool)

	// SetMetadata sets the dynamic metadata value of the stream.
	// @Param metadataNamespace the metadata namespace.
	// @Param key the metadata key.
	// @Param value the metadata value. Only string/int/float are supported.
	SetMetadata(metadataNamespace, key string, value any)

	// GetFilterState retrieves the serialized filter state value of the stream.
	// @Param key the filter state key.
	// @Return the filter state value if found, otherwise nil.
	GetFilterState(key string) ([]byte, bool)

	// SetFilterState sets the serialized filter state value of the stream.
	// @Param key the filter state key.
	// @Param value the filter state value.
	SetFilterState(key string, value []byte)

	// GetAttributeString retrieves the string attribute value of the stream.
	// @Param key the attribute key.
	// @Return the attribute value if found, otherwise nil.
	GetAttributeString(attributeID AttributeID) (string, bool)

	// GetAttributeNumber retrieves the float attribute value of the stream.
	// @Param key the attribute key.
	// @Return the attribute value if found, otherwise nil.
	GetAttributeNumber(attributeID AttributeID) (float64, bool)

	// GetData retrieves internal data stored for cross-phase communication.
	// This data is not included in DynamicMetadata responses.
	// @Param key the data key.
	// @Return the data value if found, otherwise nil.
	GetData(key string) any

	// SetData sets internal data for cross-phase communication.
	// This data is not included in DynamicMetadata responses.
	// @Param key the data key.
	// @Param value the data value.
	SetData(key string, value any)

	// SendLocalResponse sends a local reply to the client and terminates the stream.
	// @Param status the HTTP status code.
	// @Param headers the response headers.
	// @Param body the response body.
	// @Param detail a short description to the response for debugging purposes.
	SendLocalResponse(status uint32, headers [][2]string, body []byte, detail string)

	// SendResponseHeaders sends response headers to the client. This is used for
	// streaming local replies.
	//
	// @Param headers the response headers.
	// @Param endOfStream whether this is the end of the stream.
	SendResponseHeaders(headers [][2]string, endOfStream bool)

	// SendResponseData sends response body data to the client. This is used for
	// streaming local replies.
	//
	// @Param body the response body data.
	// @Param endOfStream whether this is the end of the stream.
	SendResponseData(body []byte, endOfStream bool)

	// SendResponseTrailers sends response trailers to the client. This is used for
	// streaming local replies.
	//
	// @Param trailers the response trailers.
	SendResponseTrailers(trailers [][2]string)

	// AddCustomFlag adds a custom flag to the stream. This flag should be very short
	// string to indicate some custom state or information of the stream.
	// @Param flag the custom flag to add.
	AddCustomFlag(flag string)

	// ContinueRequest continues the request stream processing.
	// NOTE: This function should only be called when the plugin chains are hung up because
	// of asynchronous operations.
	ContinueRequest()

	// ContinueResponse continues the response stream processing.
	// NOTE: This function should only be called when the plugin chains are hung up because
	// of asynchronous operations.
	ContinueResponse()

	// ClearRouteCache clears the cached route for the stream.
	ClearRouteCache()

	// RequestHeaders retrieves the request headers.
	// @Return the request headers.
	RequestHeaders() HeaderMap

	// BufferedRequestBody retrieves the buffered request body in the chain.
	// NOTE: Different with the headers and trailers, because of the streaming processing,
	// the request body is not always fully buffered. So this function only retrieves the
	// currently buffered body in the chain. And the latest newly received body chunk is passed
	// as the parameter to OnRequestBody. Only when endOfStream is true or OnRequestTrailers is
	// called, the full request body is received.
	// @Return the buffered request body.
	BufferedRequestBody() BodyBuffer

	// RequestTrailers retrieves the request trailers.
	// @Return the request trailers.
	RequestTrailers() HeaderMap

	// ResponseHeaders retrieves the response headers.
	// @Return the response headers.
	ResponseHeaders() HeaderMap

	// BufferedResponseBody retrieves the buffered response body in the chain.
	// NOTE: Different with the headers and trailers, because of the streaming processing,
	// the request body is not always fully buffered. So this function only retrieves the
	// currently buffered body in the chain. And the latest newly received body chunk is passed
	// as the parameter to OnResponseBody. Only when endOfStream is true or OnResponseTrailers is
	// called, the full request body is received.
	// @Return the buffered response body.
	BufferedResponseBody() BodyBuffer

	// ResponseTrailers retrieves the response trailers.
	// @Return the response trailers.
	ResponseTrailers() HeaderMap

	// GetMostSpecificConfig retrieves the most specific route configuration for the stream.
	GetMostSpecificConfig() any

	// GetScheduler retrieves the scheduler related to this stream plugin for asynchronous
	// operations.
	//
	// NOTE: This MUST only be called during OnRequest* or OnResponse* callbacks. But then the
	// returned Scheduler can be used later even outside of the callbacks and even at other
	// threads.
	GetScheduler() Scheduler

	// Log will log the given message via the host environment's logging mechanism.
	Log(level LogLevel, format string, args ...any)

	// HttpCallout performs an HTTP call to an external service. The call is asynchronous, and the
	// response will be delivered via the provided callback.
	// @Param cluster the cluster (target) name to which the HTTP call will be made.
	// @Param headers the HTTP headers to be sent with the request.
	// @Param body the HTTP body to be sent with the request.
	// @Param timeoutMs the timeout in milliseconds for the HTTP call.
	// @Param callback the callback function to be invoked when the response is received or an
	// error occurs.
	// The callback function receives the response headers, body, and an error if any occurred.
	//
	// @Return the result of the HTTP callout initialization and the callout ID. Non-success results
	// indicate that the callout failed to start.
	//
	// NOTE: This method should only be called during OnRequest* or OnResponse* callbacks or
	// scheduled functions via the Scheduler. By this way we can ensure this is only be called
	// in the thread where the stream plugin is being processed.
	HttpCallout(cluster string, headers [][2]string, body []byte, timeoutMs uint64,
		cb HttpCalloutCallback) (HttpCalloutInitResult, uint64)

	// StartHttpStream starts a new HTTP stream to an external service. The stream is asynchronous,
	// and the response will be delivered via the provided callback.
	// @Param cluster the cluster (target) name to which the HTTP stream will be made.
	// @Param headers the initial HTTP headers to be sent with the request.
	// @Param body the initial HTTP body to be sent with the request.
	// @Param endOfStream whether this is the end of the stream.
	// @Param timeoutMs the timeout in milliseconds for the HTTP stream.
	// @Param callback the callback interface to handle the stream events.
	//
	// @Return the result of the HTTP stream initialization and the stream ID. Non-success results
	// indicate that the stream failed to start.
	//
	// NOTE: This method should only be called during OnRequest* or OnResponse* callbacks or
	// scheduled functions via the Scheduler. By this way we can ensure this is only be called
	// in the thread where the stream plugin is being processed.
	StartHttpStream(cluster string, headers [][2]string, body []byte, endOfStream bool, timeoutMs uint64,
		cb HttpStreamCallback) (HttpCalloutInitResult, uint64)

	// SendHttpStreamData sends data on an existing HTTP stream.
	// @Param streamID the ID of the HTTP stream.
	// @Param body the HTTP body to be sent with the request.
	// @Param endOfStream whether this is the end of the stream.
	//
	// @Return whether the data was successfully sent.
	//
	// NOTE: This method should only be called during OnRequest* or OnResponse* callbacks or
	// scheduled functions via the Scheduler. By this way we can ensure this is only be called
	// in the thread where the stream plugin is being processed.
	SendHttpStreamData(streamID uint64, body []byte, endOfStream bool) bool

	// SendHttpStreamTrailers sends trailers on an existing HTTP stream.
	// @Param streamID the ID of the HTTP stream.
	// @Param trailers the HTTP trailers to be sent with the request.
	//
	// @Return whether the trailers were successfully sent.
	//
	// NOTE: This method should only be called during OnRequest* or OnResponse* callbacks or
	// scheduled functions via the Scheduler. By this way we can ensure this is only be called
	// in the thread where the stream plugin is being processed.
	SendHttpStreamTrailers(streamID uint64, trailers [][2]string) bool

	// ResetHttpStream resets an existing HTTP stream.
	// @Param streamID the ID of the HTTP stream.
	//
	// NOTE: This method should only be called during OnRequest* or OnResponse* callbacks or
	// scheduled functions via the Scheduler. By this way we can ensure this is only be called
	// in the thread where the stream plugin is being processed.
	ResetHttpStream(streamID uint64)

	// SetDownstreamWatermarkCallbacks sets the downstream watermark callbacks for the stream.
	// @Param callbacks the downstream watermark callbacks.
	SetDownstreamWatermarkCallbacks(callbacks DownstreamWatermarkCallbacks)

	// ClearDownstreamWatermarkCallbacks unsets the downstream watermark callbacks for the stream.
	ClearDownstreamWatermarkCallbacks()

	// RecordValue records the given value to the histogram metric.
	// @Param id the histogram metric id.
	// @Param value the value to be recorded.
	// @Param tagsValues the optional tag values associated with the metric. The order and size
	// of the tag values must match the tag keys defined when the metric was created.
	RecordHistogramValue(id MetricID, value uint64, tagsValues ...string) MetricsResult

	// SetValue sets the given value to the gauge metric.
	// @Param id the gauge metric id.
	// @Param value the value to be set.
	// @Param tagsValues the optional tag values associated with the metric. The order and size
	// of the tag values must match the tag keys defined when the metric was created.
	SetGaugeValue(id MetricID, value uint64, tagsValues ...string) MetricsResult

	// IncrementGaugeValue adds the given value to the gauge metric.
	// @Param id the gauge metric id.
	// @Param value the value to be added.
	// @Param tagsValues the optional tag values associated with the metric. The order and size
	// of the tag values must match the tag keys defined when the metric was created.
	IncrementGaugeValue(id MetricID, value uint64, tagsValues ...string) MetricsResult

	// DecrementGaugeValue subtracts the given value from the gauge metric.
	// @Param id the gauge metric id.
	// @Param value the value to be subtracted.
	// @Param tagsValues the optional tag values associated with the metric. The order and size
	// of the tag values must match the tag keys defined when the metric was created.
	DecrementGaugeValue(id MetricID, value uint64, tagsValues ...string) MetricsResult

	// IncrementCounterValue adds the given value to the counter metric.
	// @Param id the counter metric id.
	// @Param value the value to be added.
	// @Param tagsValues the optional tag values associated with the metric. The order and size
	// of the tag values must match the tag keys defined when the metric was created.
	IncrementCounterValue(id MetricID, value uint64, tagsValues ...string) MetricsResult
}

type MetricID uint64
type MetricsResult uint32

const (
	MetricsSuccess MetricsResult = iota
	MetricsNotFound
	MetricsInvalidTags
	MetricsFrozen
)

type HttpFilterConfigHandle interface {
	// Log will log the given message via the host environment's logging mechanism.
	Log(level LogLevel, format string, args ...any)

	// DefineHistogram creates a histogram metric with the given name, and tag keys.
	// @Param name the name of the metric.
	// @Param tagKeys the optional tag keys for the metric.
	// @Return the histogram metric id. This metric can never be used after the plugin
	// config is unloaded.
	DefineHistogram(name string, tagKeys ...string) (MetricID, MetricsResult)

	// DefineGauge creates a gauge metric with the given name, description, and tag keys.
	// @Param name the name of the metric.
	// @Param tagKeys the optional tag keys for the metric.
	// @Return the gauge metric id. This metric can never be used after the plugin
	// config is unloaded.
	DefineGauge(name string, tagKeys ...string) (MetricID, MetricsResult)

	// DefineCounter creates a counter metric with the given name, description, and tag keys.
	// @Param name the name of the metric.
	// @Param tagKeys the optional tag keys for the metric.
	// @Return the counter metric id. This metric can never be used after the plugin
	// config is unloaded.
	DefineCounter(name string, tagKeys ...string) (MetricID, MetricsResult)
}
