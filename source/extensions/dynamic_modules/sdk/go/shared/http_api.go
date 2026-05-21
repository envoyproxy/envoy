//go:generate mockgen -source=http_api.go -destination=mocks/mock_http_api.go -package=mocks
package shared

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
	// LocalReplyStatusContinue indicates that the local reply should continue to be sent after all
	// filters are informed.
	LocalReplyStatusContinue LocalReplyStatus = 0
	// LocalReplyStatusContinueAndResetStream indicates that the local reply notification should
	// continue to all filters, but the stream should be reset instead of sending the local reply.
	LocalReplyStatusContinueAndResetStream LocalReplyStatus = 1
	LocalReplyStatusDefault                LocalReplyStatus = LocalReplyStatusContinue
)

// HttpFilter is the interface to implement your own plugin logic. This is a simplified version and could
// not implement flexible stream control. But it should be enough for most of the use cases.
type HttpFilter interface {
	// OnRequestHeaders will be called when the request headers are received.
	// Returns the status to control the plugin chain processing.
	OnRequestHeaders(headers HeaderMap, endOfStream bool) HeadersStatus

	// OnRequestBody will be called when the request body are received. This may be called multiple times.
	// Returns the status to control the plugin chain processing.
	OnRequestBody(body BodyBuffer, endOfStream bool) BodyStatus

	// OnRequestTrailers will be called when the request trailers are received.
	// Returns the status to control the plugin chain processing.
	OnRequestTrailers(trailers HeaderMap) TrailersStatus

	// OnResponseHeaders will be called when the response headers are received.
	// Returns the status to control the plugin chain processing.
	OnResponseHeaders(headers HeaderMap, endOfStream bool) HeadersStatus

	// OnResponseBody will be called when the response body is received. This may be called multiple
	// times.
	// Returns the status to control the plugin chain processing.
	OnResponseBody(body BodyBuffer, endOfStream bool) BodyStatus

	// OnResponseTrailers will be called when the response trailers are received.
	// Returns the status to control the plugin chain processing.
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
	//
	// details is a short description of why the local reply is being sent (e.g.
	// "buffer overflow", "rate limit exceeded"). The buffer aliases Envoy memory; copy
	// before retaining past this call. resetImminent is true if Envoy is going to reset
	// the stream after this call.
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

func (p *EmptyHttpFilter) OnDestroy() {
}

func (p *EmptyHttpFilter) OnLocalReply(responseCode uint32, details UnsafeEnvoyBuffer, resetImminent bool) LocalReplyStatus {
	return LocalReplyStatusDefault
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

// CounterSnapshot is one counter in a MetricSnapshot.
// The Name field shares memory with Envoy's buffer and is only valid during the
// enclosing OnFlush callback — copy it with ToString() if you need to keep it.
type CounterSnapshot struct {
	Name  UnsafeEnvoyBuffer
	Value uint64
	Delta uint64
}

// GaugeSnapshot is one gauge in a MetricSnapshot. Name is only valid during the
// enclosing OnFlush callback; see CounterSnapshot for details.
type GaugeSnapshot struct {
	Name  UnsafeEnvoyBuffer
	Value uint64
}

// TextReadoutSnapshot is one text readout in a MetricSnapshot. Name and Value
// are only valid during the enclosing OnFlush callback.
type TextReadoutSnapshot struct {
	Name  UnsafeEnvoyBuffer
	Value UnsafeEnvoyBuffer
}

// MetricSnapshot gives the stats sink random access to all counters, gauges, and
// text readouts in a single flush cycle. Implementations are provided by the
// runtime; modules only consume this interface.
//
// NOTE: the returned UnsafeEnvoyBuffer fields point into Envoy-owned memory
// that lives only for the duration of the OnFlush call. Copy any data you need
// to retain.
type MetricSnapshot interface {
	// CounterCount returns the number of counters in the snapshot.
	CounterCount() uint64
	// GetCounter returns the counter at the given index. If the index is out of
	// range, the second return value is false.
	GetCounter(index uint64) (CounterSnapshot, bool)

	// GaugeCount returns the number of gauges in the snapshot.
	GaugeCount() uint64
	// GetGauge returns the gauge at the given index.
	GetGauge(index uint64) (GaugeSnapshot, bool)

	// TextReadoutCount returns the number of text readouts in the snapshot.
	TextReadoutCount() uint64
	// GetTextReadout returns the text readout at the given index.
	GetTextReadout(index uint64) (TextReadoutSnapshot, bool)
}

// StatSinkHandle is passed to StatSink methods. Today it only exposes logging;
// it exists so future capabilities (e.g. worker index, per-sink shared data)
// can be added without breaking existing modules.
type StatSinkHandle interface {
	Log(level LogLevel, format string, args ...any)
}

// StatSink is the per-config stats sink instance. A single StatSink lives for
// the lifetime of the DynamicModuleStatsSink config in Envoy, and its methods
// are called on every flush and for every histogram observation.
//
// Thread-safety: OnFlush is serialized on Envoy's stats flush thread. However,
// OnHistogramComplete is called SYNCHRONOUSLY from whichever worker thread
// recorded the histogram sample, so implementations must be thread-safe and
// fast. Buffer or batch if observation volume is high.
type StatSink interface {
	// OnFlush is called periodically (every stats_flush_interval) with a full
	// snapshot of metrics that passed the SinkPredicates filter.
	OnFlush(snapshot MetricSnapshot)

	// OnHistogramComplete is called synchronously for every histogram
	// observation. The name buffer is only valid for the duration of this call.
	OnHistogramComplete(name UnsafeEnvoyBuffer, value uint64)

	// OnDestroy is called when the stats sink config is being torn down. This
	// is the place to close sockets, flush batches, etc.
	OnDestroy()
}

// EmptyStatSink is a no-op StatSink. Embed it to get forward-compatible
// defaults for methods you don't care about.
type EmptyStatSink struct{}

func (s *EmptyStatSink) OnFlush(snapshot MetricSnapshot) {
}

func (s *EmptyStatSink) OnHistogramComplete(name UnsafeEnvoyBuffer, value uint64) {
}

func (s *EmptyStatSink) OnDestroy() {
}

// StatSinkConfigFactory parses the configuration for one stats sink and builds
// a StatSink. It runs once per sink config on the main thread at server start.
// Implementations should be stateless — per-config state lives on the returned
// StatSink.
type StatSinkConfigFactory interface {
	// Create parses the sink configuration and returns a StatSink instance, or
	// an error if the configuration is invalid. Returning a nil StatSink with
	// no error is also treated as a failure.
	//
	// @Param handle  A handle for runtime services (logging today).
	// @Param config  The bytes passed via the `sink_config` field of the
	//                DynamicModuleStatsSink proto. Encoding depends on the
	//                Any type used in the config (e.g. raw bytes for
	//                BytesValue, JSON for Struct).
	Create(handle StatSinkHandle, config []byte) (StatSink, error)
}

// EmptyStatSinkConfigFactory builds an EmptyStatSink. Useful for testing.
type EmptyStatSinkConfigFactory struct{}

func (f *EmptyStatSinkConfigFactory) Create(handle StatSinkHandle, unparsedConfig []byte) (StatSink, error) {
	return &EmptyStatSink{}, nil
}
