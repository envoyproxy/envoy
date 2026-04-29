//go:generate mockgen -source=listener_api.go -destination=mocks/mock_listener_api.go -package=mocks
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
