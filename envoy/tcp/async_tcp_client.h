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
   * event callback registered via setAsyncTcpClientCallbacks(). If the callbacks
   * needs to be changed before reconnecting, it is required to set the callbacks
   * again, before calling to connect() to attempting to reconnect.
   * @returns true if a new client has created and the connection is in progress.
   * @returns false if an underlying client exists and is connected or connecting.
   */
  virtual bool connect() PURE;

  /**
   * Close the client. It closes the connection based on close type.
   * The underlying connection will be defer deleted when a Close event is received.
   * Abrt/NoFlush will abortively closes the connection discarding any unsent data.
   * @param type the connection close type.
   */
  virtual void close(Network::ConnectionCloseType type) PURE;

  /**
   * @return the detected close type from socket.
   */
  virtual Network::DetectedCloseType detectedCloseType() const PURE;

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

  /**
   * @return the streamInfo of the current connection if there is any.
   */
  virtual OptRef<StreamInfo::StreamInfo> getStreamInfo() PURE;
};

using AsyncTcpClientPtr = std::unique_ptr<AsyncTcpClient>;

} // namespace Tcp
} // namespace Envoy
