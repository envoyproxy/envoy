#pragma once

#include "envoy/http/conn_pool.h"
#include "envoy/network/connection.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/tcp/upstream.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Tcp {
namespace Tcp {

class TcpUpstream : public Envoy::Tcp::GenericUpstream {
public:
  TcpUpstream(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& data,
              Envoy::Tcp::ConnectionPool::UpstreamCallbacks& callbacks);

  // Tcp::GenericUpstream
  bool readDisable(bool disable) override;
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void addBytesSentCallback(Network::Connection::BytesSentCb cb) override;
  Envoy::Tcp::ConnectionPool::ConnectionData*
  onDownstreamEvent(Network::ConnectionEvent event) override;

private:
  Envoy::Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
};

// An implementation of ConnectionHandle which works with the Tcp::ConnectionPool.
class TcpConnectionHandle : public Envoy::Tcp::ConnectionHandle,
                            public Envoy::Tcp::ConnectionPool::Callbacks {
public:
  TcpConnectionHandle(Envoy::Tcp::ConnectionPool::Cancellable* handle,
                      Envoy::Tcp::ConnectionPool::UpstreamCallbacks& upstream_callbacks,
                      Envoy::Tcp::GenericUpstreamPoolCallbacks& generic_pool_callbacks)
      : tcp_upstream_handle_(handle), tcp_upstream_callbacks_(upstream_callbacks),
        generic_pool_callbacks_(generic_pool_callbacks) {}

  ~TcpConnectionHandle() override {
    if (tcp_upstream_handle_ != nullptr) {
      tcp_upstream_handle_->cancel(Envoy::Tcp::ConnectionPool::CancelPolicy::CloseExcess);
    }
  }

  void cancel() override {
    if (tcp_upstream_handle_ != nullptr) {
      tcp_upstream_handle_->cancel(Envoy::Tcp::ConnectionPool::CancelPolicy::CloseExcess);
      tcp_upstream_handle_ = nullptr;
    }
  }

  void complete() override { tcp_upstream_handle_ = nullptr; }
  bool hasFailure() override { return has_failure_; }

  Envoy::Tcp::GenericUpstreamSharedPtr upstream() override { return tcp_upstream_; }

  bool failingOnPool() override { return tcp_upstream_handle_ == nullptr; }

  bool isConnecting() override {
    return tcp_upstream_handle_ != nullptr && tcp_upstream_ == nullptr;
  }

  // Envoy::Tcp::ConnectionPool::Callbacks
  void onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Envoy::Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolFailure(Envoy::Tcp::ConnectionPool::PoolFailureReason reason,
                     Envoy::Upstream::HostDescriptionConstSharedPtr host) override;

  void setUpstreamHandle(Envoy::Tcp::ConnectionPool::Cancellable* upstream_handle) {
    ASSERT(tcp_upstream_handle_ == nullptr);
    tcp_upstream_handle_ = upstream_handle;
  }

private:
  // The handle to cancel the callback to set the tcp upstream.
  Envoy::Tcp::ConnectionPool::Cancellable* tcp_upstream_handle_{};
  Envoy::Tcp::ConnectionPool::UpstreamCallbacks& tcp_upstream_callbacks_;
  Envoy::Tcp::GenericUpstreamPoolCallbacks& generic_pool_callbacks_;
  std::shared_ptr<TcpUpstream> tcp_upstream_{};
  bool has_failure_{false};
};

} // namespace Tcp
} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy