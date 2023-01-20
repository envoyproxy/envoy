#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/network/connection.h"

namespace Envoy {
namespace Tcp {

/*
 * AsyncTcpClientCallbacks for peer connection callbacks and data.
 */
class AsyncTcpClientCallbacks : public Network::ConnectionCallbacks {
public:
  ~AsyncTcpClientCallbacks() override = default;

  /*
   * Invoked when data is delivered from the peer connection.
   * @param data supplies data from the upstream
   * @param end_stream whether the data is the last data frame
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
   * event callback registered via addAsyncTcpClientCallbacks().
   */
  virtual bool connect() PURE;

  /**
   * Close the client, the underlying connection will also be defer deleted when
   * a Close event is received.
   */
  virtual void close() PURE;

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
  virtual void addAsyncTcpClientCallbacks(AsyncTcpClientCallbacks& callbacks) PURE;

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
