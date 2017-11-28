#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Network {

/**
 * Action that should occur on a connection after I/O.
 */
enum class PostIoAction { Close, KeepOpen };

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
 * Callbacks used by transport sockets to communicate with connection
 */
class TransportSocketCallbacks {
 public:
  virtual ~TransportSocketCallbacks() {}

  virtual int fd() PURE;

  virtual Connection& connection() PURE;

  virtual bool shouldDrainReadBuffer() PURE;

  virtual void setReadBufferReady() PURE;

  virtual void raiseEvent(ConnectionEvent event) PURE;
};

class TransportSocket {
public:
  virtual ~TransportSocket() {}

  virtual void setTransportSocketCallbacks(TransportSocketCallbacks& callbacks) PURE;
  virtual std::string protocol() const PURE;
  virtual bool canFlushClose() PURE;
  virtual void closeSocket(Network::ConnectionEvent event) PURE;
  virtual IoResult doRead(Buffer::Instance& buffer) PURE;
  virtual IoResult doWrite(Buffer::Instance& buffer) PURE;
  virtual void onConnected() PURE;
};

typedef std::unique_ptr<TransportSocket> TransportSocketPtr;



} // Network
} // Envoy
