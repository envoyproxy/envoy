package shared

// UdpListenerFilter is the interface to implement your own UDP listener filter logic.
type UdpListenerFilter interface {
	// OnData is called when a UDP packet is received.
	OnData() UdpListenerFilterStatus

	// OnDestroy is called when Envoy destroys the UDP listener filter instance.
	OnDestroy()
}

// EmptyUdpListenerFilter provides no-op UDP listener filter hooks with default continue behavior.
type EmptyUdpListenerFilter struct{}

// OnData implements UdpListenerFilter.
func (f *EmptyUdpListenerFilter) OnData() UdpListenerFilterStatus {
	return UdpListenerFilterStatusDefault
}

// OnDestroy implements UdpListenerFilter.
func (f *EmptyUdpListenerFilter) OnDestroy() {}

// UdpListenerFilterFactory creates per-worker UDP listener filters.
type UdpListenerFilterFactory interface {
	// Create constructs the UdpListenerFilter for one Envoy worker thread.
	Create(handle UdpListenerFilterHandle) UdpListenerFilter

	// OnDestroy is called when Envoy destroys this factory, usually after configuration has been
	// replaced and the listener using it has drained.
	OnDestroy()
}

// EmptyUdpListenerFilterFactory returns EmptyUdpListenerFilter instances.
type EmptyUdpListenerFilterFactory struct{}

// Create implements UdpListenerFilterFactory.
func (f *EmptyUdpListenerFilterFactory) Create(UdpListenerFilterHandle) UdpListenerFilter {
	return &EmptyUdpListenerFilter{}
}

// OnDestroy implements UdpListenerFilterFactory.
func (f *EmptyUdpListenerFilterFactory) OnDestroy() {}

// UdpListenerFilterConfigFactory parses configuration and returns a thread-safe filter factory.
type UdpListenerFilterConfigFactory interface {
	// Create parses unparsedConfig and returns the UdpListenerFilterFactory used for this listener.
	Create(handle UdpListenerFilterConfigHandle,
		unparsedConfig []byte) (UdpListenerFilterFactory, error)
}

// EmptyUdpListenerFilterConfigFactory returns EmptyUdpListenerFilterFactory instances.
type EmptyUdpListenerFilterConfigFactory struct{}

// Create implements UdpListenerFilterConfigFactory.
func (f *EmptyUdpListenerFilterConfigFactory) Create(UdpListenerFilterConfigHandle,
	[]byte) (UdpListenerFilterFactory, error) {
	return &EmptyUdpListenerFilterFactory{}, nil
}
