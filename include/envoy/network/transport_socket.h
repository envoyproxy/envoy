#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/network/connection.h"
#include "envoy/ssl/connection.h"

namespace Envoy {
namespace Network {

/**
 * Action that should occur on a connection after I/O.
 */
enum class PostIoAction {
  // Close the connection.
  Close,
  // Keep the connection open.
  KeepOpen
};

/**
 * Result of each I/O event.
 */
struct IoResult {
  PostIoAction action_;

  /**
   * Number of bytes processed by the I/O event.
   */
  uint64_t bytes_processed_;

  /**
   * True if an end-of-stream was read from a connection. This
   * can only be true for read operations.
   */
  bool end_stream_read_;
};

/**
 * Callbacks used by transport socket instances to communicate with connection.
 */
class TransportSocketCallbacks {
public:
  virtual ~TransportSocketCallbacks() {}

  /**
   * @return int the file descriptor associated with the connection.
   */
  virtual int fd() const PURE;

  /**
   * @return Network::Connection& the connection interface.
   */
  virtual Network::Connection& connection() PURE;

  /**
   * @return bool whether the read buffer should be drained. This is used to enforce yielding for
   *         configured read limits.
   */
  virtual bool shouldDrainReadBuffer() PURE;

  /**
   * Mark read buffer ready to read in the event loop. This is used when yielding following
   * shouldDrainReadBuffer().
   */
  virtual void setReadBufferReady() PURE;

  /**
   * Raise a connection event to the connection. This can be used by a secure socket (e.g. TLS)
   * to raise a connected event when handshake is done.
   * @param event supplies the connection event
   */
  virtual void raiseEvent(ConnectionEvent event) PURE;
};

/**
 * A transport socket that does actual read / write. It can also do some transformations on
 * the data (e.g. TLS).
 */
class TransportSocket {
public:
  virtual ~TransportSocket() {}

  /**
   * Called by connection once to initialize the transport socket callbacks that the transport
   * socket should use.
   * @param callbacks supplies the callbacks instance.
   */
  virtual void setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) PURE;

  /**
   * @return std::string the protocol to use as selected by network level negotiation. (E.g., ALPN).
   *         If network level negotation is not supported by the connection or no protocol
   *         has been negotiated the empty string is returned.
   */
  virtual std::string protocol() const PURE;

  /**
   * @return bool whether the socket can be flushed and closed.
   */
  virtual bool canFlushClose() PURE;

  /**
   * Closes the transport socket.
   * @param event supplies the connection event that is closing the socket.
   */
  virtual void closeSocket(Network::ConnectionEvent event) PURE;

  /**
   *
   * @param buffer supplies the buffer to read to.
   * @return IoResult the result of the read action.
   */
  virtual IoResult doRead(Buffer::Instance& buffer) PURE;

  /**
   * @param buffer supplies the buffer to write from
   * @param end_stream supplies whether this is the end of the stream. If true and all
   *        data in buffer is written, the connection will be half-closed.
   * @return IoResult the result of the write action.
   */
  virtual IoResult doWrite(Buffer::Instance& buffer, bool end_stream) PURE;

  /**
   * Called when underlying transport is established.
   */
  virtual void onConnected() PURE;

  /**
   * @return the SSL connection data if this is an SSL connection, or nullptr if it is not.
   */
  virtual Ssl::Connection* ssl() PURE;

  /**
   * @return the const SSL connection data if this is an SSL connection, or nullptr if it is not.
   */
  virtual const Ssl::Connection* ssl() const PURE;
};

typedef std::unique_ptr<TransportSocket> TransportSocketPtr;

/**
 * A factory for creating transport socket. It will be associated to filter chains and clusters.
 */
class TransportSocketFactory {
public:
  virtual ~TransportSocketFactory() {}

  /**
   * @return bool whether the transport socket implements secure transport.
   */
  virtual bool implementsSecureTransport() const PURE;

  /**
   * @return Network::TransportSocketPtr a transport socket to be passed to connection.
   */
  virtual TransportSocketPtr createTransportSocket() const PURE;
};

typedef std::unique_ptr<TransportSocketFactory> TransportSocketFactoryPtr;

} // namespace Network
} // namespace Envoy
