//go:generate mockgen -source=api.go -destination=mocks/mock_api.go -package=mocks
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

	// OnStreamComplete is called when the stream is closed. This is a good place to clean up any
	// resources.
	OnStreamComplete()
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

// HttpFilterFactory is the factory interface for creating stream plugins.
// This is used to create instances of the stream plugin at runtime when a new request is received.
// The implementation of this interface should be thread-safe and hold the parsed configuration.
type HttpFilterFactory interface {
	Create(handle HttpFilterHandle) HttpFilter
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
	return nil, nil
}

func (f *EmptyHttpFilterConfigFactory) CreatePerRoute(unparsedConfig []byte) (any, error) {
	return nil, nil
}
