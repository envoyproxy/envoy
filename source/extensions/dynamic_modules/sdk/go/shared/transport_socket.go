//go:generate mockgen -source=transport_socket.go -destination=mocks/mock_transport_socket.go -package=mocks
package shared

// Transport socket SDK surface for dynamic modules.
//
// Mirrors the Rust SDK's `transport_socket` module. A transport socket sits between Envoy's
// connection layer and the raw I/O handle, transforming bytes in both directions (e.g.,
// implementing TLS, custom framing, or compression). The module reads and writes via the raw
// I/O handle, then drives the connection's read/write buffers using the handle methods.
//
// A module exposes a transport socket by implementing TransportSocketFactoryConfigFactory and
// registering it from an init() function via sdk.RegisterTransportSocketFactoryConfigFactories.

// TransportSocketPostIoAction specifies what should happen on the connection after an I/O
// operation. Corresponds to envoy_dynamic_module_type_transport_socket_post_io_action /
// Network::PostIoAction.
type TransportSocketPostIoAction uint32

const (
	// TransportSocketPostIoActionKeepOpen — keep the connection open.
	TransportSocketPostIoActionKeepOpen TransportSocketPostIoAction = iota
	// TransportSocketPostIoActionClose — close the connection.
	TransportSocketPostIoActionClose
)

// TransportSocketIoResult is returned from DoRead and DoWrite. Corresponds to
// envoy_dynamic_module_type_transport_socket_io_result / Network::IoResult.
type TransportSocketIoResult struct {
	// Action is the post-I/O action to take.
	Action TransportSocketPostIoAction
	// BytesProcessed is the number of bytes the transport processed in this call (after
	// transformation).
	BytesProcessed uint64
	// EndStreamRead indicates that the read produced an end-of-stream signal.
	EndStreamRead bool
}

// TransportSocket is the per-connection module-side transport socket.
type TransportSocket interface {
	// SetCallbacks is called once before any I/O operations to supply the transport socket
	// with its Envoy callbacks. The handle remains valid for the lifetime of the transport
	// socket.
	SetCallbacks(handle TransportSocketHandle)

	// OnConnected is called when the underlying transport is established. For TLS this is
	// when the TCP connection is up but BEFORE the handshake; for plain TCP it's the
	// connection event itself.
	OnConnected(handle TransportSocketHandle)

	// DoRead is called when data is to be read from the connection and decrypted/transformed
	// into the read buffer. The module reads from handle.IoHandle, transforms, and appends
	// to handle.AddToReadBuffer.
	DoRead(handle TransportSocketHandle) TransportSocketIoResult

	// DoWrite is called when data is to be written to the connection from the write buffer.
	// writeBufferLength is the length of the write buffer at the time of the call;
	// endStream is true on a half-close after the full write.
	DoWrite(handle TransportSocketHandle, writeBufferLength uint64, endStream bool) TransportSocketIoResult

	// OnClose is called when the transport socket is being closed. event is the connection
	// event that caused the close.
	OnClose(handle TransportSocketHandle, event NetworkConnectionEvent)

	// GetProtocol returns the negotiated application-level protocol (e.g., from ALPN), or an
	// empty slice if no protocol was negotiated.
	//
	// The runtime hands the bytes directly to Envoy without copying; the returned memory must
	// remain valid until the transport socket is destroyed (or until the next GetProtocol
	// call, whichever comes first).
	GetProtocol() []byte

	// GetFailureReason returns a description of the last failure on the transport socket, or
	// an empty slice if there is no failure.
	//
	// Memory lifetime: same as GetProtocol.
	GetFailureReason() []byte

	// CanFlushClose returns true if the transport socket can be flushed and closed.
	CanFlushClose() bool

	// OnDestroy is called when the transport socket is destroyed.
	OnDestroy()
}

// EmptyTransportSocket is a no-op TransportSocket. DoRead/DoWrite return KeepOpen with 0
// bytes; CanFlushClose returns true; everything else is a no-op.
type EmptyTransportSocket struct{}

func (*EmptyTransportSocket) SetCallbacks(_ TransportSocketHandle)                              {}
func (*EmptyTransportSocket) OnConnected(_ TransportSocketHandle)                               {}
func (*EmptyTransportSocket) DoRead(_ TransportSocketHandle) TransportSocketIoResult            { return TransportSocketIoResult{Action: TransportSocketPostIoActionKeepOpen} }
func (*EmptyTransportSocket) DoWrite(_ TransportSocketHandle, _ uint64, _ bool) TransportSocketIoResult { return TransportSocketIoResult{Action: TransportSocketPostIoActionKeepOpen} }
func (*EmptyTransportSocket) OnClose(_ TransportSocketHandle, _ NetworkConnectionEvent)         {}
func (*EmptyTransportSocket) GetProtocol() []byte                                               { return nil }
func (*EmptyTransportSocket) GetFailureReason() []byte                                          { return nil }
func (*EmptyTransportSocket) CanFlushClose() bool                                               { return true }
func (*EmptyTransportSocket) OnDestroy()                                                        {}

// TransportSocketFactory creates per-connection TransportSocket instances. Implementations must
// be safe for concurrent calls.
type TransportSocketFactory interface {
	// Create creates a TransportSocket for a new connection.
	Create(handle TransportSocketHandle) TransportSocket

	// OnDestroy is called when the factory is destroyed.
	OnDestroy()
}

// EmptyTransportSocketFactory is a no-op TransportSocketFactory.
type EmptyTransportSocketFactory struct{}

func (*EmptyTransportSocketFactory) Create(_ TransportSocketHandle) TransportSocket {
	return &EmptyTransportSocket{}
}
func (*EmptyTransportSocketFactory) OnDestroy() {}

// TransportSocketFactoryConfigFactory is the top-level factory the module registers via
// sdk.RegisterTransportSocketFactoryConfigFactories.
type TransportSocketFactoryConfigFactory interface {
	// Create parses unparsedConfig and returns a TransportSocketFactory. isUpstream is true
	// when this configuration is for upstream connections, false for downstream.
	Create(name string, unparsedConfig []byte, isUpstream bool) (TransportSocketFactory, error)
}

// EmptyTransportSocketFactoryConfigFactory is a no-op TransportSocketFactoryConfigFactory.
type EmptyTransportSocketFactoryConfigFactory struct{}

func (*EmptyTransportSocketFactoryConfigFactory) Create(_ string, _ []byte, _ bool) (TransportSocketFactory, error) {
	return &EmptyTransportSocketFactory{}, nil
}

// TransportSocketHandle is the per-socket handle used by the module to interact with the raw
// I/O handle and the connection's read/write buffers.
type TransportSocketHandle interface {
	// IoHandleRead reads up to len(buffer) bytes from the raw socket into buffer. Returns the
	// number of bytes actually read and a system errno (0 on success, negative on failure
	// such as -EAGAIN).
	IoHandleRead(buffer []byte) (int64, int64)

	// IoHandleWrite writes len(buffer) bytes from buffer to the raw socket. Returns the
	// number of bytes actually written and a system errno (0 on success, negative on failure).
	IoHandleWrite(buffer []byte) (int64, int64)

	// IoHandleFD returns the native OS file descriptor for the I/O handle, or -1 if the
	// handle does not wrap a native socket.
	IoHandleFD() int32

	// DrainReadBuffer drains length bytes from the front of the connection's read buffer.
	DrainReadBuffer(length uint64)

	// AddToReadBuffer appends data to the connection's read buffer. This is how the transport
	// delivers transformed inbound bytes to the rest of the filter chain.
	AddToReadBuffer(data []byte)

	// ReadBufferLength returns the current length of the connection's read buffer.
	ReadBufferLength() uint64

	// DrainWriteBuffer drains length bytes from the front of the connection's write buffer.
	// The transport calls this after consuming bytes from the write buffer for transmission.
	DrainWriteBuffer(length uint64)

	// GetWriteBufferSlices returns the chunks of the connection's write buffer.
	//
	// NOTE: The buffers are owned by Envoy and only valid for the duration of the current
	// callback. Copy if you need to keep them.
	GetWriteBufferSlices() []UnsafeEnvoyBuffer

	// WriteBufferLength returns the current length of the connection's write buffer.
	WriteBufferLength() uint64

	// RaiseEvent raises a connection event on the connection (e.g., Connected after TLS
	// handshake completes).
	RaiseEvent(event NetworkConnectionEvent)

	// ShouldDrainReadBuffer returns whether the read buffer should be drained to enforce read
	// limits and yielding.
	ShouldDrainReadBuffer() bool

	// SetIsReadable marks the transport socket as readable so that a read will be scheduled
	// on a future event-loop iteration.
	SetIsReadable()

	// FlushWriteBuffer attempts to drain a non-empty write buffer to the underlying transport.
	FlushWriteBuffer()
}
