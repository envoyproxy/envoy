#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/network/connection.h"

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
};

/**
 * Callbacks used by transport socket instances to communicate with connection
 */
class TransportSocketCallbacks {
 public:
  virtual ~TransportSocketCallbacks() {}

  /**
   * @return the file descriptor associated with the connection.
   */
  virtual int fd() PURE;

  /**
   * @return the connection interface.
   */
  virtual Connection& connection() PURE;

  /**
   * @return return whether the read buffer should be drained
   */
  virtual bool shouldDrainReadBuffer() PURE;

  /**
   * Mark read buffer ready to read in the event loop. This is used when yielding following
   * shouldDrainReadBuffer().
   */
  virtual void setReadBufferReady() PURE;

  /**
   * Raise a connection event to the connection.
   * @param event supplies the connection event
   */
  virtual void raiseEvent(ConnectionEvent event) PURE;
};


class TransportSocket {
public:
  virtual ~TransportSocket() {}

  /**
   * Called by connection once to intiialize the transport socket callbacks that the transport
   * socket should use.
   * @param callbacks supplies the callbacks instance.
   */
  virtual void setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) PURE;

  /**
   * @return the protocol to use as selected by network level negotiation. (E.g., ALPN).
   *         If network level negotation is not supported by the connection or no protocol
   *         has been negotiated the empty string is returned.
   */
  virtual std::string protocol() const PURE;

  /**
   * @return whether the socket can be flush closed.
   */
  virtual bool canFlushClose() PURE;

  /**
   * Closes the transport socket.
   * @param event suuplies the connection event that closing the socket.
   */
  virtual void closeSocket(Network::ConnectionEvent event) PURE;

  /**
   *
   * @param buffer supplies the buffer read to.
   * @return the IoResult of the read action.
   */
  virtual IoResult doRead(Buffer::Instance& buffer) PURE;

  /**
   * @param buffer supplies the buffer to write from
   * @return the IoResult of the write action.
   */
  virtual IoResult doWrite(Buffer::Instance& buffer) PURE;

  /**
   * Called when connection established.
   */
  virtual void onConnected() PURE;
};

typedef std::unique_ptr<TransportSocket> TransportSocketPtr;



} // Network
} // Envoy
