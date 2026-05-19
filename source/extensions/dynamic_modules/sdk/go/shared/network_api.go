package shared

// NetworkFilter is the interface to implement your own network filter logic.
type NetworkFilter interface {
	// OnNewConnection is called once a TCP connection has been accepted and the filter instance has
	// been created for it.
	//
	// Returning NetworkFilterStatusContinue lets iteration proceed to later filters immediately.
	// Returning NetworkFilterStatusStop pauses iteration until the filter later calls
	// NetworkFilterHandle.ContinueReading.
	OnNewConnection() NetworkFilterStatus

	// OnRead is called when data has been read from the downstream side and is about to continue
	// through the read filter chain toward the upstream.
	//
	// data exposes the current read buffer owned by Envoy. endOfStream is true when the downstream
	// side has half-closed and this callback is processing the final read data. Returning
	// NetworkFilterStatusStop pauses iteration until ContinueReading is called.
	OnRead(data NetworkBuffer, endOfStream bool) NetworkFilterStatus

	// OnWrite is called when data is about to be written in the upstream -> downstream direction.
	//
	// data exposes the current write buffer owned by Envoy. endOfStream is true when this callback
	// is processing the final write-side data. Returning NetworkFilterStatusStop pauses iteration
	// until ContinueReading is called.
	OnWrite(data NetworkBuffer, endOfStream bool) NetworkFilterStatus

	// OnEvent is called for connection lifecycle events such as connect, remote close, and local
	// close.
	OnEvent(event NetworkConnectionEvent)

	// OnDestroy is called when the filter instance is being destroyed and should release any
	// resources tied to the connection.
	OnDestroy()

	// OnAboveWriteBufferHighWatermark is called when the connection write buffer crosses above its
	// configured high watermark.
	OnAboveWriteBufferHighWatermark()

	// OnBelowWriteBufferLowWatermark is called when the connection write buffer drops from above
	// the high watermark to below the low watermark again.
	OnBelowWriteBufferLowWatermark()
}

type EmptyNetworkFilter struct {
}

func (p *EmptyNetworkFilter) OnNewConnection() NetworkFilterStatus {
	return NetworkFilterStatusDefault
}

func (p *EmptyNetworkFilter) OnRead(data NetworkBuffer, endOfStream bool) NetworkFilterStatus {
	return NetworkFilterStatusDefault
}

func (p *EmptyNetworkFilter) OnWrite(data NetworkBuffer, endOfStream bool) NetworkFilterStatus {
	return NetworkFilterStatusDefault
}

func (p *EmptyNetworkFilter) OnEvent(event NetworkConnectionEvent) {
}

func (p *EmptyNetworkFilter) OnDestroy() {
}

func (p *EmptyNetworkFilter) OnAboveWriteBufferHighWatermark() {
}

func (p *EmptyNetworkFilter) OnBelowWriteBufferLowWatermark() {
}

// NetworkFilterFactory is the factory interface for creating network filters.
// The implementation of this interface should be thread-safe and hold the parsed configuration.
type NetworkFilterFactory interface {
	// Create constructs the per-connection NetworkFilter instance for a newly accepted TCP
	// connection.
	//
	// Returning nil causes filter creation to fail and the connection to be closed.
	Create(handle NetworkFilterHandle) NetworkFilter

	// OnDestroy is called when Envoy is destroying this factory, usually after configuration has
	// been replaced and all connections using it have drained.
	OnDestroy()
}

type EmptyNetworkFilterFactory struct {
}

func (f *EmptyNetworkFilterFactory) Create(handle NetworkFilterHandle) NetworkFilter {
	return &EmptyNetworkFilter{}
}

func (f *EmptyNetworkFilterFactory) OnDestroy() {
}

// NetworkFilterConfigFactory is the factory interface for creating network filter configurations.
// The implementation of this interface should be thread-safe and usually stateless.
type NetworkFilterConfigFactory interface {
	// Create parses the supplied configuration bytes and returns a thread-safe
	// NetworkFilterFactory for subsequent connections.
	//
	// Returning an error rejects the filter configuration.
	Create(handle NetworkFilterConfigHandle, unparsedConfig []byte) (NetworkFilterFactory, error)
}

type EmptyNetworkFilterConfigFactory struct {
}

func (f *EmptyNetworkFilterConfigFactory) Create(handle NetworkFilterConfigHandle,
	unparsedConfig []byte) (NetworkFilterFactory, error) {
	return &EmptyNetworkFilterFactory{}, nil
}
