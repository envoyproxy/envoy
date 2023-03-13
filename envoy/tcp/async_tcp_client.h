#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Tcp {

/*
 * AsyncTcpClientOptions for the creation of async tcp client;
 */
struct AsyncTcpClientOptions {
  AsyncTcpClientOptions(bool enable_half_close) : enable_half_close(enable_half_close){};
  bool enable_half_close;
};

using AsyncTcpClientOptionsConstSharedPtr = std::shared_ptr<const AsyncTcpClientOptions>;

/*
 * AsyncTcpClientCallbacks for peer connection callbacks and data.
 */
class AsyncTcpClientCallbacks : public Network::ConnectionCallbacks {
public:
  ~AsyncTcpClientCallbacks() override = default;

  /*
   * Invoked when data is delivered from the peer connection.
   * @param data supplies data from the peer.
   * @param end_stream whether the data is the last data frame.
   */
  virtual void onData(Buffer::Instance& data, bool end_stream) PURE;
};

/**
 * An async TCP client with connection to peers.
 */
class AsyncTcpClient {
public:
  virtual ~AsyncTcpClient() = default;

  /**
   * Connect to a remote host. Errors or connection events are reported via the
   * event callback registered via setAsyncTcpClientCallbacks().
   */
  virtual bool connect() PURE;

  /**
   * Close the client. It abortively closes the connection discarding any unsent data.
   * The underlying connection will be defer deleted when a Close event is received.
   */
  virtual void close() PURE;

  /**
   * Close the client. It closes the connection based on close type.
   * @param type the connection close type.
   */
  virtual void close(Network::ConnectionCloseType type) PURE;

  /**
   * Write data through the client.
   * @param data the bufferred data.
   * @param end_stream indicates if this is the end of the stream, half close
   * should be enabled by setting end_stream to true.
   */
  virtual void write(Buffer::Instance& data, bool end_stream) PURE;

  /**
   * Disable socket reads on the connection, applying external back pressure.
   * @param disable supplies TRUE is reads should be disabled, FALSE if they
   * should be enabled.
   */
  virtual void readDisable(bool disable) PURE;

  /**
   * Add AsyncTcpClientCallbacks to the client
   * @param callbacks the client callbacks.
   */
  virtual void setAsyncTcpClientCallbacks(AsyncTcpClientCallbacks& callbacks) PURE;

  /**
   * @return Event::Dispatcher& the dispatcher backing this client.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * @return if the client connects to a peer host.
   */
  virtual bool connected() PURE;
};

using AsyncTcpClientPtr = std::unique_ptr<AsyncTcpClient>;

} // namespace Tcp
} // namespace Envoy
