#pragma once

#include "envoy/network/connection.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace TcpProxy {

// Interface for a generic ConnectionHandle, which can wrap a TcpConnectionHandle
// or an HttpConnectionHandle
class ConnectionHandle {
public:
  virtual ~ConnectionHandle() {}
  // Cancel the conn pool request and close any excess pending requests.
  virtual void cancel() PURE;
};

// An implementation of ConnectionHandle which works with the Tcp::ConnectionPool.
class TcpConnectionHandle : public ConnectionHandle {
public:
  TcpConnectionHandle(Tcp::ConnectionPool::Cancellable* handle) : upstream_handle_(handle) {}

  void cancel() override {
    upstream_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::CloseExcess);
  }

private:
  Tcp::ConnectionPool::Cancellable* upstream_handle_{};
};

// Interface for a generic Upstream, which can communicate with a TCP or HTTP
// upstream.
class GenericUpstream {
public:
  virtual ~GenericUpstream() {}
  // Calls readDisable on the upstream connection. Returns false if readDisable could not be
  // performed (e.g. if the connection is closed)
  virtual bool readDisable(bool disable) PURE;
  // Encodes data upstream.
  virtual void encodeData(Buffer::Instance& data, bool end_stream) PURE;
  // Adds a callback to be called when the data is sent to the kernel.
  virtual void addBytesSentCallback(Network::Connection::BytesSentCb cb) PURE;
  // Called when a Network::ConnectionEvent is received on the downstream connection, to allow the
  // upstream to do any cleanup.
  virtual Tcp::ConnectionPool::ConnectionData*
  onDownstreamEvent(Network::ConnectionEvent event) PURE;
};

class TcpUpstream : public GenericUpstream {
public:
  TcpUpstream(Tcp::ConnectionPool::ConnectionDataPtr&& data,
              Tcp::ConnectionPool::UpstreamCallbacks& callbacks);

  // GenericUpstream
  bool readDisable(bool disable) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void addBytesSentCallback(Network::Connection::BytesSentCb cb) override;
  Tcp::ConnectionPool::ConnectionData* onDownstreamEvent(Network::ConnectionEvent event) override;

private:
  Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
};

} // namespace TcpProxy
} // namespace Envoy
