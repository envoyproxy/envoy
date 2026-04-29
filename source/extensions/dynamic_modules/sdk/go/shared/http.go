//go:generate mockgen -source=http.go -destination=mocks/mock_http.go -package=mocks
package shared

// HTTP filter SDK surface for dynamic modules.
//
// Cross-surface primitives (UnsafeEnvoyBuffer, LogLevel, MetricID, AttributeID, Scheduler,
// HttpCalloutInitResult/Result/Callback, HttpStreamCallback/ResetReason, SocketOption*,
// ClusterHostCounts, HttpHeaderType) live in types.go.

type HeadersStatus int32

const (
	// '2' is preserved for ContinueAndDontEndStream and is not exposed here.

	// HeadersStatusContinue indicates that the headers can continue to be processed by
	// next plugin in the chain and nothing will be stopped.
	HeadersStatusContinue HeadersStatus = 0
	// HeadersStatusStop indicates that the headers processing should stop at this plugin.
	// And when the body or trailers are received, the onRequestBody or onRequestTrailers
	// of this plugin will be called. And the filter chain will continue or still hang
	// based on the returned status of onRequestBody or onRequestTrailers.
	// Of course the continueRequestStream or continueResponseStream can be called to continue
	// the processing manually.
	HeadersStatusStop HeadersStatus = 1
	// HeadersStatusStopAndBuffer indicates that the headers processing should stop at this plugin.
	// And even if the body or trailers are received, the onRequestBody or onRequestTrailers
	// of this plugin will NOT be called and the body will be buffered. The only way to continue
	// the processing is to call continueRequestStream or continueResponseStream manually.
	// This is useful when you want to wait a certain condition to be met before continuing
	// the processing (For example, waiting for the result of an asynchronous operation).
	HeadersStatusStopAllAndBuffer HeadersStatus = 3
	// Similar to HeadersStatusStopAllAndBuffer. But when there are too big body data buffered,
	// the HeadersStatusStopAllAndBuffer will result in 413 (Payload Too Large) response to the
	// client. But with this status, the watermarking will be used to disable reading from client
	// or server.
	HeadersStatusStopAllAndWatermark HeadersStatus = 4
	HeadersStatusDefault             HeadersStatus = HeadersStatusContinue
)

type BodyStatus int32

const (
	// BodyStatusContinue indicates that the body can continue to be processed by next plugin
	// in the chain. And if the onRequestHeaders or onResponseHeaders of this plugin returned
	// HeadersStatusStop before, the headers processing will continue.
	BodyStatusContinue BodyStatus = 0
	// BodyStatusStopAndBuffer indicates that the body processing should stop at this plugin.
	// And the body will be buffered.
	BodyStatusStopAndBuffer BodyStatus = 1
	// BodyStatusStopAndWatermark indicates that the body processing should stop at this plugin.
	// And watermarking will be used to disable reading from client or server if there are too
	// big body data buffered.
	BodyStatusStopAndWatermark BodyStatus = 2
	// BodyStatusStopNoBuffer indicates that the body processing should stop at this plugin.
	// No body data will be buffered.
	BodyStatusStopNoBuffer BodyStatus = 3
	BodyStatusDefault      BodyStatus = BodyStatusContinue
)

type TrailersStatus int32

const (
	// TrailersStatusContinue indicates that the trailers can continue to be processed by next plugin
	// in the chain. And if the onRequestHeaders, onResponseHeaders, onRequestBody or onResponseBody
	// of this plugin have returned stop status before, the processing will continue after this.
	TrailersStatusContinue TrailersStatus = 0
	// TrailersStatusStop indicates that the trailers processing should stop at this plugin. The
	// only way to continue the processing is to call continueRequestStream or continueResponseStream
	// manually.
	TrailersStatusStop    TrailersStatus = 1
	TrailersStatusDefault TrailersStatus = TrailersStatusContinue
)

// LocalReplyStatus is returned from HttpFilter.OnLocalReply to control whether Envoy should
// send the local reply to the client or reset the stream instead.
type LocalReplyStatus int32

const (
	// LocalReplyStatusContinue indicates that the local reply should continue to be sent
	// after all filters are informed.
	LocalReplyStatusContinue LocalReplyStatus = 0
	// LocalReplyStatusContinueAndResetStream indicates that the local reply notification
	// should continue to all filters, but the stream should be reset instead of sending
	// the local reply.
	LocalReplyStatusContinueAndResetStream LocalReplyStatus = 1
	LocalReplyStatusDefault                LocalReplyStatus = LocalReplyStatusContinue
)

// HttpFilter is the interface to implement your own plugin logic. This is a simplified version and could
// not implement flexible stream control. But it should be enough for most of the use cases.
type HttpFilter interface {
	// OnRequestHeaders will be called when the request headers are received.
	// @Param headers the request headers.
	// @Param endOfStream whether this is the end of the stream.
	// @Return HeadersStatus the status to control the plugin chain processing.
	OnRequestHeaders(headers HeaderMap, endOfStream bool) HeadersStatus

	// OnRequestBody will be called when the request body are received. This may be called multiple times.
	// @Param body the request body.
	// @Param endOfStream whether this is the end of the stream.
	// @Return BodyStatus the status to control the plugin chain processing.
	OnRequestBody(body BodyBuffer, endOfStream bool) BodyStatus

	// OnRequestTrailers will be called when the request trailers are received.
	// @Param trailers the request trailers.
	// @Return TrailersStatus the status to control the plugin chain processing.
	OnRequestTrailers(trailers HeaderMap) TrailersStatus

	// OnResponseHeaders will be called when the response headers are received.
	// @Param headers the response headers.
	// @Param endOfStream whether this is the end of the stream.
	// @Return HeadersStatus the status to control the plugin chain processing.
	OnResponseHeaders(headers HeaderMap, endOfStream bool) HeadersStatus

	// OnResponseBody will be called when the response body is received. This may be called multiple
	// times.
	// @Param body the response body.
	// @Param endOfStream whether this is the end of the stream.
	// @Return BodyStatus the status to control the plugin chain processing.
	OnResponseBody(body BodyBuffer, endOfStream bool) BodyStatus

	// OnResponseTrailers will be called when the response trailers are received.
	// @Param trailers the response trailers.
	// @Return TrailersStatus the status to control the plugin chain processing.
	OnResponseTrailers(trailers HeaderMap) TrailersStatus

	// OnStreamComplete is called when the stream processing is complete and before access logs
	// are flushed.
	// This is a good place to do any final processing or cleanup before the request is fully
	// completed.
	OnStreamComplete()

	// OnDestroy is called when the HTTP filter instance is being destroyed. This is called
	// after OnStreamComplete and access logs are flushed. This is a good place to release
	// any per-stream resources.
	OnDestroy()

	// OnLocalReply is called when a local reply is being sent. The filter can either let the
	// reply proceed (LocalReplyStatusContinue) or ask Envoy to reset the stream instead
	// (LocalReplyStatusContinueAndResetStream). This is invoked before the reply leaves
	// Envoy and before any stream reset.
	// @Param responseCode the HTTP status code of the local reply.
	// @Param details a short description of why the local reply is being sent (e.g.,
	// "buffer overflow", "rate limit exceeded"). The buffer aliases Envoy memory; copy
	// before retaining past this call.
	// @Param resetImminent true if Envoy is going to reset the stream after this call.
	OnLocalReply(responseCode uint32, details UnsafeEnvoyBuffer, resetImminent bool) LocalReplyStatus
}

type EmptyHttpFilter struct {
}

func (p *EmptyHttpFilter) OnRequestHeaders(headers HeaderMap, endOfStream bool) HeadersStatus {
	return HeadersStatusDefault
}

func (p *EmptyHttpFilter) OnRequestBody(body BodyBuffer, endOfStream bool) BodyStatus {
	return BodyStatusDefault
}

func (p *EmptyHttpFilter) OnRequestTrailers(trailers HeaderMap) TrailersStatus {
	return TrailersStatusDefault
}

func (p *EmptyHttpFilter) OnResponseHeaders(headers HeaderMap, endOfStream bool) HeadersStatus {
	return HeadersStatusDefault
}

func (p *EmptyHttpFilter) OnResponseBody(body BodyBuffer, endOfStream bool) BodyStatus {
	return BodyStatusDefault
}

func (p *EmptyHttpFilter) OnResponseTrailers(trailers HeaderMap) TrailersStatus {
	return TrailersStatusDefault
}

func (p *EmptyHttpFilter) OnStreamComplete() {
}

func (p *EmptyHttpFilter) OnLocalReply(_ uint32, _ UnsafeEnvoyBuffer, _ bool) LocalReplyStatus {
	return LocalReplyStatusDefault
}

func (p *EmptyHttpFilter) OnDestroy() {
}

// HttpFilterFactory is the factory interface for creating stream plugins.
// This is used to create instances of the stream plugin at runtime when a new request is received.
// The implementation of this interface should be thread-safe and hold the parsed configuration.
type HttpFilterFactory interface {
	// Create creates a HttpFilter instance.
	Create(handle HttpFilterHandle) HttpFilter

	// OnDestroy is called when the factory is being destroyed. This is a good place to clean up any
	// resources. This usually happens when the configuration is updated and all existing streams
	// using this factory are closed.
	OnDestroy()
}

type EmptyHttpFilterFactory struct {
}

func (f *EmptyHttpFilterFactory) Create(handle HttpFilterHandle) HttpFilter {
	return &EmptyHttpFilter{}
}

func (f *EmptyHttpFilterFactory) OnDestroy() {
}

// HttpFilterConfigFactory is the factory interface for creating stream plugin configurations.
// This is used to create
// PluginConfig based on the unparsed configuration. The HttpFilterConfigFactory should parse the unparsedConfig
// and create a PluginFactory instance.
// The implementation of this interface should be thread-safe and be stateless in most cases.
type HttpFilterConfigFactory interface {
	// Create creates a HttpFilterFactory based on the unparsed configuration.
	Create(handle HttpFilterConfigHandle, unparsedConfig []byte) (HttpFilterFactory, error)

	// CreatePerRoute creates a per-route configuration based on the unparsed configuration.
	CreatePerRoute(unparsedConfig []byte) (any, error)
}

type EmptyHttpFilterConfigFactory struct {
}

func (f *EmptyHttpFilterConfigFactory) Create(handle HttpFilterConfigHandle,
	unparsedConfig []byte) (HttpFilterFactory, error) {
	return &EmptyHttpFilterFactory{}, nil
}

func (f *EmptyHttpFilterConfigFactory) CreatePerRoute(unparsedConfig []byte) (any, error) {
	return nil, nil
}

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
	// @Param source the metadata source type.
	// @Param metadataNamespace the metadata namespace.
	// @Param key the metadata key.
	// @Return the metadata value if found, otherwise an empty UnsafeEnvoyBuffer.
	GetMetadataString(source MetadataSourceType, metadataNamespace, key string) (UnsafeEnvoyBuffer, bool)

	// GetMetadataNumber retrieves the dynamic metadata number value of the stream.
	// @Param source the metadata source type.
	// @Param metadataNamespace the metadata namespace.
	// @Param key the metadata key.
	// @Return the metadata value if found, otherwise nil.
	GetMetadataNumber(source MetadataSourceType, metadataNamespace, key string) (float64, bool)

	// GetMetadataBool retrieves the dynamic metadata bool value of the stream.
	// @Param source the metadata source type.
	// @Param metadataNamespace the metadata namespace.
	// @Param key the metadata key.
	// @Return the metadata value and true if found, otherwise false.
	GetMetadataBool(source MetadataSourceType, metadataNamespace, key string) (bool, bool)

	// SetMetadata sets the dynamic metadata value of the stream.
	// @Param metadataNamespace the metadata namespace.
	// @Param key the metadata key.
	// @Param value the metadata value. Only string/int/float/bool are supported.
	SetMetadata(metadataNamespace, key string, value any)

	// GetMetadataKeys retrieves all keys in the given metadata namespace.
	// @Param source the metadata source type.
	// @Param metadataNamespace the metadata namespace.
	// @Return the list of keys in the namespace, or nil if the namespace does not exist.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetMetadataKeys(source MetadataSourceType, metadataNamespace string) []UnsafeEnvoyBuffer

	// GetMetadataNamespaces retrieves all namespace names in the metadata.
	// @Param source the metadata source type.
	// @Return the list of namespace names, or nil if no namespaces exist.
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
	// @Param key the filter state key.
	// @Return the filter state value if found, otherwise an empty UnsafeEnvoyBuffer.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetFilterState(key string) (UnsafeEnvoyBuffer, bool)

	// SetFilterState sets the serialized filter state value of the stream.
	// @Param key the filter state key.
	// @Param value the filter state value.
	SetFilterState(key string, value []byte)

	// GetAttributeString retrieves the string attribute value of the stream.
	// @Param attributeID the attribute ID.
	// @Return the attribute value if found, otherwise an empty UnsafeEnvoyBuffer.
	// NOTE: The memory of underlying data may not be managed by Go GC. So you should
	// copy the data if you need to keep it and use it later.
	GetAttributeString(attributeID AttributeID) (UnsafeEnvoyBuffer, bool)

	// GetAttributeNumber retrieves the integer attribute value of the stream.
	// @Param attributeID the attribute ID.
	// @Return the attribute value if found, otherwise (0, false).
	GetAttributeNumber(attributeID AttributeID) (uint64, bool)

	// GetAttributeBool retrieves the bool attribute value of the stream.
	// @Param attributeID the attribute ID.
	// @Return the attribute value and true if found, otherwise false.
	GetAttributeBool(attributeID AttributeID) (bool, bool)

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

	// RefreshRouteCluster clears only the cluster selection for the current route without
	// clearing the entire route cache.
	// This is a subset of ClearRouteCache. Use this when a filter modifies headers that affect
	// cluster selection but not the route itself.
	RefreshRouteCluster()

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

	// ReceivedRequestBody retrieves the latest received request body chunk in the OnRequestBody callback.
	// NOTE: This is only valid in the OnRequestBody callback, and it retrieves the latest received
	// body chunk that triggers the callback. For other callbacks or outside of the callbacks, you
	// should use BufferedRequestBody to get the currently buffered body in the chain.
	ReceivedRequestBody() BodyBuffer

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

	// ReceivedResponseBody retrieves the latest received response body chunk in the OnResponseBody callback.
	// NOTE: This is only valid in the OnResponseBody callback, and it retrieves the latest received
	// body chunk that triggers the callback. For other callbacks or outside of the callbacks, you
	// should use BufferedResponseBody to get the currently buffered body in the chain.
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

	// GetWorkerIndex returns the worker thread index assigned to the current HTTP filter.
	// This can be used by the module to manage worker-specific resources.
	GetWorkerIndex() uint32

	// GetFilterStateTyped retrieves the serialized bytes of a typed filter state object stored
	// under the given key. Unlike GetFilterState, this calls serializeAsString on the registered
	// typed object, so it works for any filter state object type (not just StringAccessor).
	// @Return the serialized value if found, otherwise an empty UnsafeEnvoyBuffer and false.
	// NOTE: The memory of the underlying data may not be managed by Go GC. Copy the data if you
	// need to keep it past the current callback.
	GetFilterStateTyped(key string) (UnsafeEnvoyBuffer, bool)

	// SetFilterStateTyped sets the typed filter state value stored under the given key. The key
	// MUST match a registered ObjectFactory; the bytes are passed to createFromBytes on that
	// factory. This is the form required for interop with built-in Envoy filters that read filter
	// state as typed objects (e.g., tcp_proxy reading PerConnectionCluster).
	// @Return true if the operation was successful, false otherwise (e.g., no factory registered
	// for the key, factory failed to create the object, or the key is read-only).
	SetFilterStateTyped(key string, value []byte) bool

	// SetSocketOptionInt sets an integer-valued socket option on the upstream or downstream
	// connection associated with the stream.
	// @Param level the socket option level (e.g., SOL_SOCKET).
	// @Param name the socket option name (e.g., SO_KEEPALIVE).
	// @Param state the socket state at which to apply the option. Ignored for already-connected
	// downstream sockets.
	// @Param direction whether to apply to the upstream or downstream connection.
	// @Param value the integer value for the option.
	// @Return true if the operation was successful, false otherwise.
	SetSocketOptionInt(level, name int64, state SocketOptionState, direction SocketDirection, value int64) bool

	// SetSocketOptionBytes sets a bytes-valued socket option on the upstream or downstream
	// connection associated with the stream.
	// @Return true if the operation was successful, false otherwise.
	SetSocketOptionBytes(level, name int64, state SocketOptionState, direction SocketDirection, value []byte) bool

	// GetSocketOptionInt retrieves the integer value of a socket option.
	// @Return the value and true if found, otherwise 0 and false.
	GetSocketOptionInt(level, name int64, state SocketOptionState, direction SocketDirection) (int64, bool)

	// GetSocketOptionBytes retrieves the bytes value of a socket option. The buffer is owned by
	// Envoy and remains valid until the filter is destroyed.
	// @Return the value and true if found, otherwise an empty UnsafeEnvoyBuffer and false.
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
	// @Return the cluster name and true if found, otherwise an empty UnsafeEnvoyBuffer and false.
	// NOTE: The memory of the underlying data may not be managed by Go GC. Copy the data if you
	// need to keep it past the current callback.
	GetClusterName() (UnsafeEnvoyBuffer, bool)

	// GetClusterHostCounts returns the host counts for the routed cluster at the given priority.
	// @Param priority the priority level to query (0 for default priority).
	// @Return the host counts and true if successful, otherwise a zero-valued struct and false.
	GetClusterHostCounts(priority uint32) (ClusterHostCounts, bool)

	// SetUpstreamOverrideHost sets a host that the upstream load balancer should select first if
	// it exists in the routed cluster. This is useful for sticky sessions or host affinity.
	// @Param host the host address to override (e.g., "10.0.0.1:8080"). Must be a valid IP address.
	// @Param strict if true, the request will fail when the override host is not available; if
	// false, normal load balancing is used as a fallback.
	// @Return true if the override was set successfully, false if the host address was invalid.
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
	// from the current callback.
	// @Return true if recreation was initiated, false otherwise (e.g., the request body has not
	// been fully received yet).
	RecreateStream(headers [][2]string) bool
}

// HttpFilterConfigHandle is the per-filter-config handle exposed to HttpFilterConfig
// implementations. It supports config-scoped logging, metric definition, and async I/O via
// HttpCallout / StartHttpStream from the main thread.
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

	// HttpCallout performs an HTTP call to an external service from the config context.
	// The call is asynchronous, and the response will be delivered via the provided callback.
	// This is similar to HttpFilterHandle.HttpCallout but runs on the main thread rather than
	// the worker thread.
	// @Param cluster the cluster (target) name to which the HTTP call will be made.
	// @Param headers the HTTP headers to be sent with the request.
	// @Param body the HTTP body to be sent with the request.
	// @Param timeoutMs the timeout in milliseconds for the HTTP call.
	// @Param callback the callback function to be invoked when the response is received.
	// @Return the result of the HTTP callout initialization and the callout ID.
	HttpCallout(cluster string, headers [][2]string, body []byte, timeoutMs uint64,
		cb HttpCalloutCallback) (HttpCalloutInitResult, uint64)

	// StartHttpStream starts a new HTTP stream to an external service from the config context.
	// The stream is asynchronous, and the response will be delivered via the provided callback.
	// This is similar to HttpFilterHandle.StartHttpStream but runs on the main thread.
	// @Param cluster the cluster (target) name.
	// @Param headers the initial HTTP headers.
	// @Param body the initial HTTP body.
	// @Param endOfStream whether this is the end of the stream.
	// @Param timeoutMs the timeout in milliseconds.
	// @Param callback the callback interface to handle the stream events.
	// @Return the result of the HTTP stream initialization and the stream ID.
	StartHttpStream(cluster string, headers [][2]string, body []byte, endOfStream bool,
		timeoutMs uint64, cb HttpStreamCallback) (HttpCalloutInitResult, uint64)

	// SendHttpStreamData sends data on an existing HTTP stream started via StartHttpStream.
	// @Param streamID the ID of the HTTP stream.
	// @Param body the HTTP body to be sent.
	// @Param endOfStream whether this is the end of the stream.
	// @Return whether the data was successfully sent.
	SendHttpStreamData(streamID uint64, body []byte, endOfStream bool) bool

	// SendHttpStreamTrailers sends trailers on an existing HTTP stream started via StartHttpStream.
	// @Param streamID the ID of the HTTP stream.
	// @Param trailers the HTTP trailers to be sent.
	// @Return whether the trailers were successfully sent.
	SendHttpStreamTrailers(streamID uint64, trailers [][2]string) bool

	// ResetHttpStream resets an existing HTTP stream started via StartHttpStream.
	// @Param streamID the ID of the HTTP stream.
	ResetHttpStream(streamID uint64)

	// GetScheduler retrieves a scheduler for deferred task execution in the config context.
	// This should be called only during the plugin configuration phase, and the returned
	// Scheduler can be used later even outside of the callbacks and at other threads.
	GetScheduler() Scheduler
}
