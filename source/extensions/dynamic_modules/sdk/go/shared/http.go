//go:generate mockgen -source=http.go -destination=mocks/mock_http.go -package=mocks
package shared

// HTTP filter SDK surface for dynamic modules — handle, buffer, header, span, and watermark
// types used by the user-facing interfaces in http_api.go.
//
// Cross-surface primitives (UnsafeEnvoyBuffer, LogLevel, MetricID, AttributeID, Scheduler,
// HttpCalloutInitResult/Result/Callback, HttpStreamCallback/ResetReason, SocketOption*,
// ClusterHostCounts, HttpHeaderType) live in types.go.

// BodyBuffer is an interface that provides access to the request and response body.
// This should be implemented by the SDK or runtime.
type BodyBuffer interface {
	// GetChunks retrieves the body content as a list of UnsafeEnvoyBuffer chunks.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetChunks() []UnsafeEnvoyBuffer

	// GetSize retrieves the total size of the body buffer.
	GetSize() uint64

	// Drain removes the specified number of bytes from the beginning of the body buffer.
	Drain(numBytes uint64)

	// Append adds the specified bytes to the end of the body buffer.
	Append(data []byte)
}

// HeaderMap is an interface that provides access to the request and response headers.
// This should be implemented by the SDK or runtime.
type HeaderMap interface {
	// Get retrieves the header values for a given key. If the key does not exist,
	// nil will be returned.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	Get(key string) []UnsafeEnvoyBuffer

	// GetOne retrieves a single header value for a given key.
	// If there are multiple values for the key, the first one will be returned.
	// If the key does not exist, an empty UnsafeEnvoyBuffer will be returned.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetOne(key string) UnsafeEnvoyBuffer

	// GetAll retrieves all header values. You should not mutate the returned slice
	// directly.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetAll() [][2]UnsafeEnvoyBuffer

	// Set sets the header value for a given key.
	Set(key string, value string)

	// Add adds a single header value for a given key.
	Add(key string, value string)

	// Remove removes the header values for a given key.
	Remove(key string)
}

// MetadataSourceType identifies which metadata source to read from. Corresponds to
// envoy_dynamic_module_type_metadata_source.
type MetadataSourceType uint32

const (
	MetadataSourceTypeDynamic MetadataSourceType = iota
	MetadataSourceTypeRoute
	MetadataSourceTypeCluster
	MetadataSourceTypeHost
	MetadataSourceTypeHostLocality
)

// HttpFilterStreamResetReason is the reason for resetting the main HTTP stream via
// HttpFilterHandle.ResetStream. This corresponds to envoy_dynamic_module_type_http_filter_stream_reset_reason
// in the dynamic module ABI.
type HttpFilterStreamResetReason uint32

const (
	// HttpFilterStreamResetReasonLocalReset indicates a local codec level reset was sent on the stream.
	HttpFilterStreamResetReasonLocalReset HttpFilterStreamResetReason = iota
	// HttpFilterStreamResetReasonLocalRefusedStreamReset indicates a local codec level refused stream
	// reset was sent on the stream (allowing for retry).
	HttpFilterStreamResetReasonLocalRefusedStreamReset
)

// Span is a tracing span associated with the current HTTP stream. It is owned by Envoy and is
// valid for the lifetime of the HTTP stream. Modules MUST NOT call Finish on the active span -
// it is managed by Envoy. Use SpawnChild to create child spans whose lifetime the module owns.
type Span interface {
	// SetTag sets a key/value tag on the span.
	SetTag(key, value string)

	// SetOperation sets the operation name on the span.
	SetOperation(operation string)

	// Log records an event on the span with the current timestamp.
	Log(event string)

	// SetSampled overrides the sampling decision for the span. If sampled is false, this span and
	// any subsequent child spans will not be reported to the tracing system.
	SetSampled(sampled bool)

	// GetBaggage retrieves a baggage value from the span. Returns the value and true if the key
	// was found, otherwise an empty buffer and false.
	// NOTE: The memory of the underlying data may not be managed by Go GC. Copy the data if you
	// need to keep it past the current callback.
	GetBaggage(key string) (UnsafeEnvoyBuffer, bool)

	// SetBaggage sets a baggage value on the span. All subsequent child spans will have access to
	// this baggage.
	SetBaggage(key, value string)

	// GetTraceID retrieves the trace ID from the span. Returns the value and true if available,
	// otherwise an empty buffer and false.
	// NOTE: The memory of the underlying data may not be managed by Go GC. Copy the data if you
	// need to keep it past the current callback.
	GetTraceID() (UnsafeEnvoyBuffer, bool)

	// GetSpanID retrieves the span ID from the span. Returns the value and true if available,
	// otherwise an empty buffer and false.
	// NOTE: The memory of the underlying data may not be managed by Go GC. Copy the data if you
	// need to keep it past the current callback.
	GetSpanID() (UnsafeEnvoyBuffer, bool)

	// SpawnChild creates a child span with the given operation name. The returned ChildSpan must
	// be finished by calling its Finish method when the module is done with it. Returns nil if
	// the child span could not be created.
	SpawnChild(operationName string) ChildSpan
}

// ChildSpan is a tracing span owned by the module. It must be finished by calling Finish when
// the module is done with it.
type ChildSpan interface {
	// SetTag sets a key/value tag on the span.
	SetTag(key, value string)

	// SetOperation sets the operation name on the span.
	SetOperation(operation string)

	// Log records an event on the span with the current timestamp.
	Log(event string)

	// SetSampled overrides the sampling decision for the span.
	SetSampled(sampled bool)

	// SetBaggage sets a baggage value on the span. All subsequent child spans will have access to
	// this baggage.
	SetBaggage(key, value string)

	// SpawnChild creates a child span from this span with the given operation name. Returns nil
	// if the child span could not be created.
	SpawnChild(operationName string) ChildSpan

	// Finish finishes and releases this span. After calling this method, the span is no longer
	// valid and must not be used. Calling Finish more than once is a no-op.
	Finish()
}

// DownstreamWatermarkCallbacks is the callback interface invoked when the downstream connection
// crosses the configured write-buffer high/low watermark.
type DownstreamWatermarkCallbacks interface {
	OnAboveWriteBufferHighWatermark()
	OnBelowWriteBufferLowWatermark()
}

// HttpFilterHandle is the interface that provides access to the plugin's context and configuration.
// This should be implemented by the SDK or runtime.
type HttpFilterHandle interface {
	// GetMetadataString retrieves the dynamic metadata string value of the stream.
	// Returns metadata value if found, otherwise an empty UnsafeEnvoyBuffer.
	GetMetadataString(source MetadataSourceType, metadataNamespace, key string) (UnsafeEnvoyBuffer, bool)

	// GetMetadataNumber retrieves the dynamic metadata number value of the stream.
	// Returns metadata value if found, otherwise nil.
	GetMetadataNumber(source MetadataSourceType, metadataNamespace, key string) (float64, bool)

	// GetMetadataBool retrieves the dynamic metadata bool value of the stream.
	// Returns metadata value and true if found, otherwise false.
	GetMetadataBool(source MetadataSourceType, metadataNamespace, key string) (bool, bool)

	// SetMetadata sets the dynamic metadata value of the stream.
	SetMetadata(metadataNamespace, key string, value any)

	// GetMetadataKeys retrieves all keys in the given metadata namespace.
	// Returns list of keys in the namespace, or nil if the namespace does not exist.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetMetadataKeys(source MetadataSourceType, metadataNamespace string) []UnsafeEnvoyBuffer

	// GetMetadataNamespaces retrieves all namespace names in the metadata.
	// Returns list of namespace names, or nil if no namespaces exist.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetMetadataNamespaces(source MetadataSourceType) []UnsafeEnvoyBuffer

	// AddMetadataListNumber appends a number value to the dynamic metadata list stored under the
	// given namespace and key. If the key does not exist, a new list is created. Returns false if
	// the key exists but is not a list, or if the metadata is not accessible.
	AddMetadataListNumber(metadataNamespace, key string, value float64) bool

	// AddMetadataListString appends a string value to the dynamic metadata list stored under the
	// given namespace and key. If the key does not exist, a new list is created. Returns false if
	// the key exists but is not a list, or if the metadata is not accessible.
	AddMetadataListString(metadataNamespace, key string, value string) bool

	// AddMetadataListBool appends a bool value to the dynamic metadata list stored under the
	// given namespace and key. If the key does not exist, a new list is created. Returns false if
	// the key exists but is not a list, or if the metadata is not accessible.
	AddMetadataListBool(metadataNamespace, key string, value bool) bool

	// GetMetadataListSize returns the number of elements in the metadata list stored under the
	// given namespace and key. Returns (0, false) if the metadata is not accessible, the namespace
	// or key does not exist, or the value is not a list.
	GetMetadataListSize(source MetadataSourceType, metadataNamespace, key string) (int, bool)

	// GetMetadataListNumber returns the number element at the given index in the metadata list
	// stored under the given namespace and key. Returns (0, false) if the metadata is not
	// accessible, the namespace or key does not exist, the value is not a list, the index is out
	// of range, or the element is not a number.
	GetMetadataListNumber(source MetadataSourceType, metadataNamespace, key string, index int) (float64, bool)

	// GetMetadataListString returns the string element at the given index in the metadata list
	// stored under the given namespace and key. Returns an empty buffer and false if the metadata is
	// not accessible, the namespace or key does not exist, the value is not a list, the index is
	// out of range, or the element is not a string.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetMetadataListString(source MetadataSourceType, metadataNamespace, key string, index int) (UnsafeEnvoyBuffer, bool)

	// GetMetadataListBool returns the bool element at the given index in the metadata list stored
	// under the given namespace and key. Returns (false, false) if the metadata is not accessible,
	// the namespace or key does not exist, the value is not a list, the index is out of range, or
	// the element is not a bool.
	GetMetadataListBool(source MetadataSourceType, metadataNamespace, key string, index int) (bool, bool)

	// GetFilterState retrieves the serialized filter state value of the stream.
	// Returns filter state value if found, otherwise an empty UnsafeEnvoyBuffer.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetFilterState(key string) (UnsafeEnvoyBuffer, bool)

	// SetFilterState sets the serialized filter state value of the stream.
	SetFilterState(key string, value []byte)

	// SetFilterStateTyped sets the typed filter state value stored under the given key. The key
	// MUST match a registered ObjectFactory; the bytes are passed to createFromBytes on that
	// factory. This is the form required for interop with built-in Envoy filters that read filter
	// state as typed objects (e.g., tcp_proxy reading PerConnectionCluster).
	// Returns true on success, or false if no factory is registered for the key, the factory
	// failed to create the object, or the key is read-only.
	SetFilterStateTyped(key string, value []byte) bool

	// GetAttributeString retrieves the string attribute value of the stream.
	// Returns attribute value if found, otherwise an empty UnsafeEnvoyBuffer.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetAttributeString(attributeID AttributeID) (UnsafeEnvoyBuffer, bool)

	// GetAttributeNumber retrieves the integer attribute value of the stream.
	// Returns attribute value if found, otherwise (0, false).
	GetAttributeNumber(attributeID AttributeID) (uint64, bool)

	// GetAttributeBool retrieves the bool attribute value of the stream.
	// Returns attribute value and true if found, otherwise false.
	GetAttributeBool(attributeID AttributeID) (bool, bool)

	// GetFilterStateTyped retrieves the serialized bytes of a typed filter state object stored
	// under the given key. Unlike GetFilterState, this calls serializeAsString on the registered
	// typed object, so it works for any filter state object type (not just StringAccessor).
	// Returns serialized value if found, otherwise an empty UnsafeEnvoyBuffer and false.
	// NOTE: The memory of the underlying data may not be managed by Go GC. Copy the data if you
	// need to keep it past the current callback.
	GetFilterStateTyped(key string) (UnsafeEnvoyBuffer, bool)

	// GetData retrieves internal data stored for cross-phase communication.
	// This data is not included in DynamicMetadata responses.
	// Returns data value if found, otherwise nil.
	GetData(key string) any

	// SetData sets internal data for cross-phase communication.
	// This data is not included in DynamicMetadata responses.
	SetData(key string, value any)

	// SendLocalResponse sends a local reply to the client and terminates the stream.
	SendLocalResponse(status uint32, headers [][2]string, body []byte, detail string)

	// SendResponseHeaders sends response headers to the client. This is used for
	// streaming local replies.
	SendResponseHeaders(headers [][2]string, endOfStream bool)

	// SendResponseData sends response body data to the client. This is used for
	// streaming local replies.
	SendResponseData(body []byte, endOfStream bool)

	// SendResponseTrailers sends response trailers to the client. This is used for
	// streaming local replies.
	SendResponseTrailers(trailers [][2]string)

	// AddCustomFlag adds a custom flag to the stream. This flag should be very short
	// string to indicate some custom state or information of the stream.
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

	// RefreshRouteCluster clears only the cluster selection for the current route without
	// clearing the entire route cache.
	// This is a subset of ClearRouteCache. Use this when a filter modifies headers that affect
	// cluster selection but not the route itself.
	RefreshRouteCluster()

	// GetWorkerIndex returns the worker thread index assigned to the current HTTP filter.
	// This can be used by the module to manage worker-specific resources.
	GetWorkerIndex() uint32

	// SetSocketOptionInt sets an integer-valued socket option on the upstream or downstream
	// connection associated with the stream. Returns true on success.
	SetSocketOptionInt(level, name int64, state SocketOptionState, direction SocketDirection, value int64) bool

	// SetSocketOptionBytes sets a bytes-valued socket option on the upstream or downstream
	// connection associated with the stream. Returns true on success.
	SetSocketOptionBytes(level, name int64, state SocketOptionState, direction SocketDirection, value []byte) bool

	// GetSocketOptionInt retrieves the integer value of a socket option.
	// Returns value and true if found, otherwise 0 and false.
	GetSocketOptionInt(level, name int64, state SocketOptionState, direction SocketDirection) (int64, bool)

	// GetSocketOptionBytes retrieves the bytes value of a socket option. The buffer is owned by
	// Envoy and remains valid until the filter is destroyed.
	// Returns value and true if found, otherwise an empty UnsafeEnvoyBuffer and false.
	// NOTE: The memory of the underlying data may not be managed by Go GC. Copy the data if you
	// need to keep it past the current callback.
	GetSocketOptionBytes(level, name int64, state SocketOptionState, direction SocketDirection) (UnsafeEnvoyBuffer, bool)

	// GetBufferLimit returns the current per-stream body buffer limit in bytes. A limit of 0
	// indicates no limit is applied.
	GetBufferLimit() uint64

	// SetBufferLimit sets the per-stream body buffer limit. It is recommended (but not required)
	// that filters only INCREASE the limit, to avoid conflicting with the buffer requirements of
	// other filters in the chain.
	SetBufferLimit(limit uint64)

	// GetActiveSpan returns the active tracing span for the stream, or nil if tracing is disabled
	// or no span is available. The returned Span is owned by Envoy; do not Finish it. Use
	// Span.SpawnChild to create module-owned child spans.
	GetActiveSpan() Span

	// GetClusterName returns the name of the cluster the current request is routed to.
	// Returns cluster name and true if found, otherwise an empty UnsafeEnvoyBuffer and false.
	// NOTE: The memory of the underlying data may not be managed by Go GC. Copy the data if you
	// need to keep it past the current callback.
	GetClusterName() (UnsafeEnvoyBuffer, bool)

	// GetClusterHostCounts returns the host counts for the routed cluster at the given priority.
	// Returns host counts and true if successful, otherwise a zero-valued struct and false.
	GetClusterHostCounts(priority uint32) (ClusterHostCounts, bool)

	// SetUpstreamOverrideHost sets a host that the upstream load balancer should select first
	// if it exists in the routed cluster. Useful for sticky sessions or host affinity. When
	// strict is false, normal load balancing is used as a fallback. Returns false if the host
	// address was invalid.
	SetUpstreamOverrideHost(host string, strict bool) bool

	// ResetStream resets the HTTP stream with the given reason and optional details. After this
	// call, no further filter callbacks will be invoked except OnDestroy.
	ResetStream(reason HttpFilterStreamResetReason, details string)

	// SendGoAwayAndClose sends a GOAWAY frame to the downstream and closes the connection. If
	// graceful is true, a graceful drain is initiated before closing.
	SendGoAwayAndClose(graceful bool)

	// RecreateStream recreates the HTTP stream, optionally with new headers (or with the original
	// headers if headers is nil). Useful for internal redirects or request retries. After a
	// successful call, the current filter chain is destroyed and the filter SHOULD return Stop
	// from the current callback. Returns false if recreation could not be initiated (e.g., the
	// request body has not been fully received yet).
	RecreateStream(headers [][2]string) bool

	// RequestHeaders retrieves the request headers.
	RequestHeaders() HeaderMap

	// BufferedRequestBody retrieves the buffered request body in the chain.
	// NOTE: Because of streaming processing, the request body is not always fully buffered.
	// This function only retrieves the currently buffered body in the chain. The latest newly
	// received body chunk is passed as the parameter to OnRequestBody. Only when endOfStream is
	// true or OnRequestTrailers is called is the full request body received.
	BufferedRequestBody() BodyBuffer

	// ReceivedRequestBody retrieves the latest received request body chunk in the OnRequestBody
	// callback.
	// NOTE: This is only valid in the OnRequestBody callback. For other callbacks or outside of
	// callbacks, use BufferedRequestBody to get the currently buffered body in the chain.
	ReceivedRequestBody() BodyBuffer

	// RequestTrailers retrieves the request trailers.
	RequestTrailers() HeaderMap

	// ResponseHeaders retrieves the response headers.
	ResponseHeaders() HeaderMap

	// BufferedResponseBody retrieves the buffered response body in the chain. See
	// BufferedRequestBody for the buffering caveats.
	BufferedResponseBody() BodyBuffer

	// ReceivedResponseBody retrieves the latest received response body chunk in the OnResponseBody
	// callback.
	// NOTE: This is only valid in the OnResponseBody callback. For other callbacks or outside of
	// callbacks, use BufferedResponseBody to get the currently buffered body in the chain.
	ReceivedResponseBody() BodyBuffer

	// ReceivedBufferedRequestBody returns true if the latest received request body is the
	// previously buffered request body. This is true when a previous filter in the chain stopped
	// and buffered the request body, then resumed, and this filter is now receiving that buffered
	// body.
	// NOTE: This is only meaningful inside the OnRequestBody callback.
	ReceivedBufferedRequestBody() bool

	// ReceivedBufferedResponseBody returns true if the latest received response body is the
	// previously buffered response body. This is true when a previous filter in the chain stopped
	// and buffered the response body, then resumed, and this filter is now receiving that buffered
	// body.
	// NOTE: This is only meaningful inside the OnResponseBody callback.
	ReceivedBufferedResponseBody() bool

	// ResponseTrailers retrieves the response trailers.
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

	// HttpCallout performs an HTTP call to an external service. The call is asynchronous; the
	// response, or an error, is delivered to the provided callback.
	//
	// Returns the initialization result and the callout ID. A non-success result indicates the
	// callout failed to start.
	//
	// NOTE: This method should only be called during OnRequest* or OnResponse* callbacks or
	// from a scheduled function, so that it runs on the thread where the stream is being
	// processed.
	HttpCallout(cluster string, headers [][2]string, body []byte, timeoutMs uint64,
		cb HttpCalloutCallback) (HttpCalloutInitResult, uint64)

	// StartHttpStream starts a new HTTP stream to an external service. The stream is asynchronous;
	// responses are delivered to the provided callback.
	//
	// Returns the initialization result and the stream ID. A non-success result indicates the
	// stream failed to start.
	//
	// NOTE: This method should only be called during OnRequest* or OnResponse* callbacks or
	// from a scheduled function, so that it runs on the thread where the stream is being
	// processed.
	StartHttpStream(cluster string, headers [][2]string, body []byte, endOfStream bool, timeoutMs uint64,
		cb HttpStreamCallback) (HttpCalloutInitResult, uint64)

	// SendHttpStreamData sends data on an existing HTTP stream. Returns true if the data was
	// sent successfully.
	//
	// NOTE: This method should only be called during OnRequest* or OnResponse* callbacks or
	// from a scheduled function, so that it runs on the thread where the stream is being
	// processed.
	SendHttpStreamData(streamID uint64, body []byte, endOfStream bool) bool

	// SendHttpStreamTrailers sends trailers on an existing HTTP stream. Returns true if the
	// trailers were sent successfully.
	//
	// NOTE: This method should only be called during OnRequest* or OnResponse* callbacks or
	// from a scheduled function, so that it runs on the thread where the stream is being
	// processed.
	SendHttpStreamTrailers(streamID uint64, trailers [][2]string) bool

	// ResetHttpStream resets an existing HTTP stream.
	//
	// NOTE: This method should only be called during OnRequest* or OnResponse* callbacks or
	// from a scheduled function, so that it runs on the thread where the stream is being
	// processed.
	ResetHttpStream(streamID uint64)

	// SetDownstreamWatermarkCallbacks sets the downstream watermark callbacks for the stream.
	SetDownstreamWatermarkCallbacks(callbacks DownstreamWatermarkCallbacks)

	// ClearDownstreamWatermarkCallbacks unsets the downstream watermark callbacks for the stream.
	ClearDownstreamWatermarkCallbacks()

	// RecordHistogramValue records the given value to the histogram metric. The order and
	// size of tagsValues must match the tag keys defined when the metric was created.
	RecordHistogramValue(id MetricID, value uint64, tagsValues ...string) MetricsResult

	// SetGaugeValue sets the given value on the gauge metric. The order and size of
	// tagsValues must match the tag keys defined when the metric was created.
	SetGaugeValue(id MetricID, value uint64, tagsValues ...string) MetricsResult

	// IncrementGaugeValue adds the given value to the gauge metric. The order and size of
	// tagsValues must match the tag keys defined when the metric was created.
	IncrementGaugeValue(id MetricID, value uint64, tagsValues ...string) MetricsResult

	// DecrementGaugeValue subtracts the given value from the gauge metric. The order and
	// size of tagsValues must match the tag keys defined when the metric was created.
	DecrementGaugeValue(id MetricID, value uint64, tagsValues ...string) MetricsResult

	// IncrementCounterValue adds the given value to the counter metric. The order and
	// size of tagsValues must match the tag keys defined when the metric was created.
	IncrementCounterValue(id MetricID, value uint64, tagsValues ...string) MetricsResult
}

// HttpFilterConfigHandle is the per-filter-config handle exposed to HttpFilterConfig
// implementations. It supports config-scoped logging, metric definition, and async I/O via
// HttpCallout / StartHttpStream from the main thread.
type HttpFilterConfigHandle interface {
	// Log will log the given message via the host environment's logging mechanism.
	Log(level LogLevel, format string, args ...any)

	// DefineHistogram creates a histogram metric with the given name, and tag keys.
	// Returns histogram metric id. This metric can never be used after the plugin
	// config is unloaded.
	DefineHistogram(name string, tagKeys ...string) (MetricID, MetricsResult)

	// DefineGauge creates a gauge metric with the given name, description, and tag keys.
	// Returns gauge metric id. This metric can never be used after the plugin
	// config is unloaded.
	DefineGauge(name string, tagKeys ...string) (MetricID, MetricsResult)

	// DefineCounter creates a counter metric with the given name, description, and tag keys.
	// Returns counter metric id. This metric can never be used after the plugin
	// config is unloaded.
	DefineCounter(name string, tagKeys ...string) (MetricID, MetricsResult)

	// HttpCallout performs an HTTP call to an external service from the config context.
	// The call is asynchronous, and the response will be delivered via the provided callback.
	// This is similar to HttpFilterHandle.HttpCallout but runs on the main thread rather than
	// the worker thread.
	// Returns result of the HTTP callout initialization and the callout ID.
	HttpCallout(cluster string, headers [][2]string, body []byte, timeoutMs uint64,
		cb HttpCalloutCallback) (HttpCalloutInitResult, uint64)

	// StartHttpStream starts a new HTTP stream to an external service from the config context.
	// The stream is asynchronous, and the response will be delivered via the provided callback.
	// This is similar to HttpFilterHandle.StartHttpStream but runs on the main thread.
	// Returns result of the HTTP stream initialization and the stream ID.
	StartHttpStream(cluster string, headers [][2]string, body []byte, endOfStream bool,
		timeoutMs uint64, cb HttpStreamCallback) (HttpCalloutInitResult, uint64)

	// SendHttpStreamData sends data on an existing HTTP stream started via StartHttpStream.
	// Returns true if the data was sent successfully.
	SendHttpStreamData(streamID uint64, body []byte, endOfStream bool) bool

	// SendHttpStreamTrailers sends trailers on an existing HTTP stream started via StartHttpStream.
	// Returns true if the trailers were sent successfully.
	SendHttpStreamTrailers(streamID uint64, trailers [][2]string) bool

	// ResetHttpStream resets an existing HTTP stream started via StartHttpStream.
	ResetHttpStream(streamID uint64)

	// GetScheduler retrieves a scheduler for deferred task execution in the config context.
	// This should be called only during the plugin configuration phase, and the returned
	// Scheduler can be used later even outside of the callbacks and at other threads.
	GetScheduler() Scheduler
}
