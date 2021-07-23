#pragma once

#include <cstdint>
#include <memory>

#include "envoy/http/codec.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "source/common/buffer/watermark_buffer.h"
#include "source/common/common/cleanup.h"
#include "source/common/common/logger.h"
#include "source/common/config/well_known_names.h"
#include "source/common/router/upstream_request.h"
#include "source/common/stream_info/stream_info_impl.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {

class TcpConnPool : public Router::GenericConnPool, public Envoy::Tcp::ConnectionPool::Callbacks {
public:
  TcpConnPool(Upstream::ThreadLocalCluster& thread_local_cluster, bool is_connect,
              const Router::RouteEntry& route_entry, absl::optional<Envoy::Http::Protocol>,
              Upstream::LoadBalancerContext* ctx) {
    ASSERT(is_connect);
    pool_data_set_ = thread_local_cluster.tcpConnPool(route_entry.priority(), ctx);
  }
  void newStream(Router::GenericConnectionPoolCallbacks* callbacks) override {
    callbacks_ = callbacks;
    // TODO(shikugawa): Not to establish connections to all selected hosts.
    // This feature must be implemented to introduce ALS conn rotation.
    upstream_handle_ = pool_data_set_[0].newConnection(*this);
  }

  bool cancelAnyPendingStream() override {
    if (upstream_handle_) {
      upstream_handle_->cancel(Envoy::Tcp::ConnectionPool::CancelPolicy::Default);
      upstream_handle_ = nullptr;
      return true;
    }
    return false;
  }
  Upstream::HostDescriptionConstSharedPtr host() const override {
    // TODO(shikugawa): Connection pool that holds multiple hosts. But this feature doesn't needed
    // for now. We should introduce multiple host data set when ALS conn rotation is implemented.
    return pool_data_set_[0].host();
  }

  bool valid() {
    // TODO(shikugawa): Valid TCP connection pool must be that has more than one pool data.
    // But it must be co-implemented with ALS rotation. As workaround, we specify valid data pool
    // is that has valid connection.
    return pool_data_set_.size() == 1;
  }

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     absl::string_view transport_failure_reason,
                     Upstream::HostDescriptionConstSharedPtr host) override {
    upstream_handle_ = nullptr;
    callbacks_->onPoolFailure(reason, transport_failure_reason, host);
  }

  void onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Upstream::HostDescriptionConstSharedPtr host) override;

private:
  Envoy::Upstream::TcpPoolDataVector pool_data_set_;
  Envoy::Tcp::ConnectionPool::Cancellable* upstream_handle_{};
  Router::GenericConnectionPoolCallbacks* callbacks_{};
};

class TcpUpstream : public Router::GenericUpstream,
                    public Envoy::Tcp::ConnectionPool::UpstreamCallbacks {
public:
  TcpUpstream(Router::UpstreamToDownstream* upstream_request,
              Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& upstream);

  // GenericUpstream
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const Envoy::Http::MetadataMapVector&) override {}
  Envoy::Http::Status encodeHeaders(const Envoy::Http::RequestHeaderMap&, bool end_stream) override;
  void encodeTrailers(const Envoy::Http::RequestTrailerMap&) override;
  void readDisable(bool disable) override;
  void resetStream() override;
  void setAccount(Buffer::BufferMemoryAccountSharedPtr) override {}

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

private:
  Router::UpstreamToDownstream* upstream_request_;
  Envoy::Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
};

} // namespace Tcp
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
