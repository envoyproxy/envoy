package shared

// UdpListenerFilter is the interface to implement your own UDP listener filter logic.
//
// Unlike TCP listener and network filters, a UDP listener filter instance is created once per
// listener per Envoy worker thread, not once per connection or session. OnData is then called for
// every datagram that worker receives, so any state kept on the implementation is shared across
// datagrams and across peers. Implementations do not need to be thread-safe: Envoy only ever calls
// into a given instance from its own worker thread.
type UdpListenerFilter interface {
	// OnData is called when a UDP datagram is received on this worker.
	//
	// The datagram itself is read and modified through the UdpListenerFilterHandle passed to
	// UdpListenerFilterFactory.Create.
	//
	// Returning UdpListenerFilterStatusContinue passes the datagram to the next UDP listener
	// filter; returning UdpListenerFilterStatusStop drops it so later filters never see it.
	OnData() UdpListenerFilterStatus

	// OnDestroy is called when the filter instance is being destroyed, which happens when the
	// listener is drained or Envoy shuts the worker down. It should release any resources tied to
	// the filter.
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
// The implementation of this interface should be thread-safe and hold the parsed configuration.
type UdpListenerFilterFactory interface {
	// Create constructs the UdpListenerFilter for one Envoy worker thread.
	//
	// Returning nil causes filter creation to fail, and Envoy passes datagrams through without
	// invoking the module on that worker.
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
// The implementation of this interface should be thread-safe and usually stateless.
type UdpListenerFilterConfigFactory interface {
	// Create parses unparsedConfig and returns the UdpListenerFilterFactory used for this listener.
	//
	// Returning an error rejects the filter configuration.
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
