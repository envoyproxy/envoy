#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/network/address.h"
#include "envoy/network/filter.h"
#include "envoy/ssl/connection.h"

namespace Envoy {
namespace Event {
class Dispatcher;
}

namespace Network {

/**
 * Events that occur on a connection. Maybe be combined.
 */
class ConnectionEvent {
public:
  static const uint32_t RemoteClose = 0x1;
  static const uint32_t LocalClose = 0x2;
  static const uint32_t Connected = 0x4;
};

/**
 * Connections have both a read and write buffer.
 */
enum class ConnectionBufferType { Read, Write };

/**
 * Network level callbacks that happen on a connection.
 */
class ConnectionCallbacks {
public:
  virtual ~ConnectionCallbacks() {}

  /**
   * Callback for connection events.
   * @param events supplies the ConnectionEvent events that occurred as a bitmask.
   */
  virtual void onEvent(uint32_t events) PURE;

  /**
   * Called when the write buffer for a connection goes over its high watermark.
   */
  virtual void onAboveWriteBufferHighWatermark() PURE;

  /**
   * Called when the write buffer for a connection goes from over its high
   * watermark to under its low watermark.
   */
  virtual void onBelowWriteBufferLowWatermark() PURE;
};

/**
 * Type of connection close to perform.
 */
enum class ConnectionCloseType {
  FlushWrite, // Flush pending write data before raising ConnectionEvent::LocalClose
  NoFlush     // Do not flush any pending data and immediately raise ConnectionEvent::LocalClose
};

/**
 * An abstract raw connection. Free the connection or call close() to disconnect.
 */
class Connection : public Event::DeferredDeletable, public FilterManager {
public:
  enum class State { Open, Closing, Closed };

  struct BufferStats {
    Stats::Counter& read_total_;
    Stats::Gauge& read_current_;
    Stats::Counter& write_total_;
    Stats::Gauge& write_current_;
  };

  virtual ~Connection() {}

  /**
   * Register callbacks that fire when connection events occur.
   */
  virtual void addConnectionCallbacks(ConnectionCallbacks& cb) PURE;

  /**
   * Close the connection.
   */
  virtual void close(ConnectionCloseType type) PURE;

  /**
   * @return Event::Dispatcher& the dispatcher backing this connection.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * @return uint64_t the unique local ID of this connection.
   */
  virtual uint64_t id() const PURE;

  /**
   * @return std::string the next protocol to use as selected by network level negotiation. (E.g.,
   *         ALPN). If network level negotation is not supported by the connection or no protocol
   *         has been negotiated the empty string is returned.
   */
  virtual std::string nextProtocol() const PURE;

  /**
   * Enable/Disable TCP NO_DELAY on the connection.
   */
  virtual void noDelay(bool enable) PURE;

  /**
   * Disable socket reads on the connection, applying external back pressure. When reads are
   * enabled again if there is data still in the input buffer it will be redispatched through
   * the filter chain.
   * @param disable supplies TRUE is reads should be disabled, FALSE if they should be enabled.
   */
  virtual void readDisable(bool disable) PURE;

  /**
   * @return bool whether reading is enabled on the connection.
   */
  virtual bool readEnabled() const PURE;

  /**
   * @return The address of the remote client.
   */
  virtual const Address::Instance& remoteAddress() const PURE;

  /**
   * @return the local address of the connection. For client connections, this is the origin
   * address. For server connections, this is the local destination address. For server connections
   * it can be different from the proxy address if the downstream connection has been redirected or
   * the proxy is operating in transparent mode.
   */
  virtual const Address::Instance& localAddress() const PURE;

  /**
   * Set the buffer stats to update when the connection's read/write buffers change. Note that
   * for performance reasons these stats are eventually consistent and may not always accurately
   * represent the buffer contents at any given point in time.
   */
  virtual void setBufferStats(const BufferStats& stats) PURE;

  /**
   * @return the SSL connection data if this is an SSL connection, or nullptr if it is not.
   */
  virtual Ssl::Connection* ssl() PURE;

  /**
   * @return the const SSL connection data if this is an SSL connection, or nullptr if it is not.
   */
  virtual const Ssl::Connection* ssl() const PURE;

  /**
   * @return State the current state of the connection.
   */
  virtual State state() const PURE;

  /**
   * Write data to the connection. Will iterate through downstream filters with the buffer if any
   * are installed.
   */
  virtual void write(Buffer::Instance& data) PURE;

  /**
   * Set a soft limit on the size of buffers for the connection.
   * For the read buffer, this limits the bytes read prior to flushing to further stages in the
   * processing pipeline.
   * For the write buffer, it sets watermarks.  When enough data is buffered it triggers a call to
   * onAboveWriteBufferHighWatermark, which allows subscribers to enforce flow control by disabling
   * reads on the socket funneling data to the write buffer.  When enough data is drained from the
   * write buffer, onBelowWriteBufferHighWatermark is called which similarly allows subscribers
   * resuming reading.
   */
  virtual void setBufferLimits(uint32_t limit) PURE;

  /**
   * Get the value set with setBufferLimits.
   */
  virtual uint32_t bufferLimit() const PURE;
};

typedef std::unique_ptr<Connection> ConnectionPtr;

/**
 * Connections capable of outbound connects.
 */
class ClientConnection : public virtual Connection {
public:
  /**
   * Connect to a remote host. Errors or connection events are reported via the event callback
   * registered via setConnectionEventCb().
   */
  virtual void connect() PURE;
};

typedef std::unique_ptr<ClientConnection> ClientConnectionPtr;

} // namespace Network
} // namespace Envoy
