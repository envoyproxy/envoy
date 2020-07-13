#pragma once
#include <memory>

#include "envoy/tcp/conn_pool.h"

namespace Envoy {
namespace TcpProxy {

// Interface for a generic Upstream, which can communicate with a TCP or HTTP
// upstream.
class GenericUpstream {
public:
  virtual ~GenericUpstream() = default;
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
using GenericUpstreamSharedPtr = std::shared_ptr<GenericUpstream>;

// The generic upstream pool callback is used to notify when the upstream is created successfully or
// failed to create.
class GenericUpstreamPoolCallbacks {
public:
  virtual ~GenericUpstreamPoolCallbacks() = default;
  virtual void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                             Upstream::HostDescriptionConstSharedPtr host) PURE;
  virtual void onPoolReady(const GenericUpstreamSharedPtr& upstream,
                           Upstream::HostDescriptionConstSharedPtr& host,
                           const Network::Address::InstanceConstSharedPtr& local_address,
                           const StreamInfo::StreamInfo& info) PURE;
};

// Interface for a generic GenericConnPool, which can wrap a TcpConnPool
// or an HttpGenericConnPool.
class GenericConnPool {
public:
  virtual ~GenericConnPool() = default;
  // Cancel the conn pool request and close any excess pending requests.
  virtual void cancelAnyPendingRequest() PURE;
  // Return true if the conn pool is not valid. This can be called when this handle is
  // created.
  virtual bool failedOnPool() PURE;
  // Return true if the conn pool is valid but fail to connect. This can be called when this handle
  // is created.
  virtual bool failedOnConnection() PURE;
};
using GenericConnPoolPtr = std::unique_ptr<GenericConnPool>;

} // namespace TcpProxy
} // namespace Envoy