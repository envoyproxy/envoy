//go:generate mockgen -source=upstream_http_tcp_bridge.go -destination=mocks/mock_upstream_http_tcp_bridge.go -package=mocks
package shared

// Upstream HTTP/TCP bridge SDK surface for dynamic modules.
//
// Mirrors the Rust SDK's `upstream_http_tcp_bridge` module. This extension implements the
// Router::GenericConnPoolFactory interface, allowing modules to transform an HTTP request into
// raw TCP data for an upstream connection and convert the TCP response back into an HTTP
// response.
//
// Lifecycle: a new UpstreamHttpTcpBridge instance is created for each HTTP request that is
// routed to a cluster configured with this bridge. Encode hooks (EncodeHeaders / EncodeData /
// EncodeTrailers) deliver the HTTP request bytes; OnUpstreamData delivers the raw TCP response.
// The module uses handle methods to read request headers, get/set upstream send data, and build
// the HTTP response (headers, body, trailers).

// UpstreamHttpTcpBridge is the per-request module-side bridge object.
type UpstreamHttpTcpBridge interface {
	// EncodeHeaders is called when the HTTP request headers are being encoded for the
	// upstream. The module can read request headers via handle.GetRequestHeaders or
	// handle.GetRequestHeader and use handle.SendUpstreamData or handle.SendResponse* to act
	// on the request. endOfStream is true when this is the final frame (header-only request).
	EncodeHeaders(handle UpstreamHttpTcpBridgeHandle, endOfStream bool)

	// EncodeData is called when the HTTP request body data is being encoded for the upstream.
	// The module can read the current body chunk via handle.GetRequestBuffer and forward
	// transformed data via handle.SendUpstreamData. endOfStream is true on the final data frame.
	EncodeData(handle UpstreamHttpTcpBridgeHandle, endOfStream bool)

	// EncodeTrailers is called when the HTTP request trailers are being encoded for the
	// upstream. The module can use handle.SendUpstreamData to forward any remaining data.
	EncodeTrailers(handle UpstreamHttpTcpBridgeHandle)

	// OnUpstreamData is called when raw TCP data is received from the upstream connection. The
	// module should read it via handle.GetResponseBuffer, process it, and emit the HTTP
	// response via handle.SendResponseHeaders / handle.SendResponseData /
	// handle.SendResponseTrailers (or the all-in-one handle.SendResponse).
	//
	// endOfStream is true if the upstream connection has closed (no more data).
	OnUpstreamData(handle UpstreamHttpTcpBridgeHandle, endOfStream bool)

	// OnDestroy is called when the per-request bridge instance is being destroyed.
	OnDestroy()
}

// EmptyUpstreamHttpTcpBridge is a no-op UpstreamHttpTcpBridge.
type EmptyUpstreamHttpTcpBridge struct{}

func (*EmptyUpstreamHttpTcpBridge) EncodeHeaders(_ UpstreamHttpTcpBridgeHandle, _ bool) {}
func (*EmptyUpstreamHttpTcpBridge) EncodeData(_ UpstreamHttpTcpBridgeHandle, _ bool)    {}
func (*EmptyUpstreamHttpTcpBridge) EncodeTrailers(_ UpstreamHttpTcpBridgeHandle)        {}
func (*EmptyUpstreamHttpTcpBridge) OnUpstreamData(_ UpstreamHttpTcpBridgeHandle, _ bool) {}
func (*EmptyUpstreamHttpTcpBridge) OnDestroy()                                          {}

// UpstreamHttpTcpBridgeFactory creates per-request bridge instances. Implementations must be
// safe for concurrent calls.
type UpstreamHttpTcpBridgeFactory interface {
	// Create creates a UpstreamHttpTcpBridge for a new request.
	Create(handle UpstreamHttpTcpBridgeHandle) UpstreamHttpTcpBridge

	// OnDestroy is called when the factory is destroyed.
	OnDestroy()
}

// EmptyUpstreamHttpTcpBridgeFactory is a no-op UpstreamHttpTcpBridgeFactory.
type EmptyUpstreamHttpTcpBridgeFactory struct{}

func (*EmptyUpstreamHttpTcpBridgeFactory) Create(_ UpstreamHttpTcpBridgeHandle) UpstreamHttpTcpBridge {
	return &EmptyUpstreamHttpTcpBridge{}
}
func (*EmptyUpstreamHttpTcpBridgeFactory) OnDestroy() {}

// UpstreamHttpTcpBridgeConfigFactory is the top-level factory the module registers via
// sdk.RegisterUpstreamHttpTcpBridgeConfigFactories.
type UpstreamHttpTcpBridgeConfigFactory interface {
	// Create parses unparsedConfig and returns a factory.
	Create(name string, unparsedConfig []byte) (UpstreamHttpTcpBridgeFactory, error)
}

// EmptyUpstreamHttpTcpBridgeConfigFactory is a no-op UpstreamHttpTcpBridgeConfigFactory.
type EmptyUpstreamHttpTcpBridgeConfigFactory struct{}

func (*EmptyUpstreamHttpTcpBridgeConfigFactory) Create(_ string, _ []byte) (UpstreamHttpTcpBridgeFactory, error) {
	return &EmptyUpstreamHttpTcpBridgeFactory{}, nil
}

// UpstreamHttpTcpBridgeHandle is the per-request handle used inside encode/upstream callbacks.
// It is valid for the lifetime of the bridge instance, but methods that read buffers (e.g.
// GetRequestBuffer / GetResponseBuffer) only return meaningful data inside the corresponding
// callback.
type UpstreamHttpTcpBridgeHandle interface {
	// ---- request headers ----

	// GetRequestHeader returns a request header value by key. index selects among multi-value
	// headers (0 = first); the second return value is the total count of values for that key.
	GetRequestHeader(key string, index uint64) (UnsafeEnvoyBuffer, uint64, bool)

	// GetRequestHeadersSize returns the number of request headers.
	GetRequestHeadersSize() uint64

	// GetRequestHeaders returns all request headers.
	//
	// NOTE: The buffers are owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep them.
	GetRequestHeaders() [][2]UnsafeEnvoyBuffer

	// ---- buffers ----

	// GetRequestBuffer returns the current request body data as a slice of chunks. During
	// EncodeData this is the current body chunk; during EncodeHeaders the buffer is initially
	// empty.
	//
	// NOTE: The buffers are owned by Envoy and only valid for the duration of the current
	// callback.
	GetRequestBuffer() []UnsafeEnvoyBuffer

	// GetResponseBuffer returns the raw TCP data received from the upstream as a slice of
	// chunks. Available during OnUpstreamData.
	//
	// NOTE: The buffers are owned by Envoy and only valid for the duration of the current
	// callback.
	GetResponseBuffer() []UnsafeEnvoyBuffer

	// ---- send to upstream ----

	// SendUpstreamData sends transformed data to the TCP upstream connection. If endOfStream
	// is true, the upstream connection is half-closed after writing.
	SendUpstreamData(data []byte, endOfStream bool)

	// ---- send to downstream ----

	// SendResponse sends a complete local HTTP response to the downstream client, ending the
	// stream. Useful for error responses or short-circuit replies that don't require upstream
	// communication.
	SendResponse(statusCode uint32, headers [][2]string, body []byte)

	// SendResponseHeaders sends response headers to the downstream client, optionally ending
	// the stream.
	SendResponseHeaders(statusCode uint32, headers [][2]string, endOfStream bool)

	// SendResponseData sends response body data to the downstream client. May be called
	// multiple times to stream data; if endOfStream is true, the stream ends after this call.
	SendResponseData(data []byte, endOfStream bool)

	// SendResponseTrailers sends response trailers to the downstream client, ending the stream.
	SendResponseTrailers(trailers [][2]string)
}
