package shared

// ListenerFilter is the interface to implement your own listener filter logic.
type ListenerFilter interface {
	// OnAccept is called when Envoy accepts a new downstream socket.
	OnAccept() ListenerFilterStatus

	// OnData is called when bytes are available in the listener filter peek buffer.
	OnData(dataLength uint64) ListenerFilterStatus

	// OnClose is called when the socket closes while this filter owns iteration.
	OnClose()

	// OnDestroy is called when Envoy destroys the listener filter instance.
	OnDestroy()

	// MaxReadBytes reports how many bytes Envoy should peek for this filter.
	MaxReadBytes() uint64
}

// EmptyListenerFilter provides no-op listener filter hooks with default continue behavior.
type EmptyListenerFilter struct{}

// OnAccept implements ListenerFilter.
func (f *EmptyListenerFilter) OnAccept() ListenerFilterStatus { return ListenerFilterStatusDefault }

// OnData implements ListenerFilter.
func (f *EmptyListenerFilter) OnData(uint64) ListenerFilterStatus {
	return ListenerFilterStatusDefault
}

// OnClose implements ListenerFilter.
func (f *EmptyListenerFilter) OnClose() {}

// OnDestroy implements ListenerFilter.
func (f *EmptyListenerFilter) OnDestroy() {}

// MaxReadBytes implements ListenerFilter.
func (f *EmptyListenerFilter) MaxReadBytes() uint64 { return 0 }

// ListenerFilterFactory creates per-connection listener filters.
type ListenerFilterFactory interface {
	// Create constructs the per-connection ListenerFilter for a newly accepted socket.
	Create(handle ListenerFilterHandle) ListenerFilter

	// OnDestroy is called when Envoy destroys this factory after draining existing users.
	OnDestroy()
}

// EmptyListenerFilterFactory returns EmptyListenerFilter instances.
type EmptyListenerFilterFactory struct{}

// Create implements ListenerFilterFactory.
func (f *EmptyListenerFilterFactory) Create(ListenerFilterHandle) ListenerFilter {
	return &EmptyListenerFilter{}
}

// OnDestroy implements ListenerFilterFactory.
func (f *EmptyListenerFilterFactory) OnDestroy() {}

// ListenerFilterConfigFactory parses configuration and returns a thread-safe filter factory.
type ListenerFilterConfigFactory interface {
	// Create parses unparsedConfig and returns the ListenerFilterFactory for new downstream sockets.
	Create(handle ListenerFilterConfigHandle, unparsedConfig []byte) (ListenerFilterFactory, error)
}

// EmptyListenerFilterConfigFactory returns EmptyListenerFilterFactory instances.
type EmptyListenerFilterConfigFactory struct{}

// Create implements ListenerFilterConfigFactory.
func (f *EmptyListenerFilterConfigFactory) Create(ListenerFilterConfigHandle,
	[]byte) (ListenerFilterFactory, error) {
	return &EmptyListenerFilterFactory{}, nil
}
