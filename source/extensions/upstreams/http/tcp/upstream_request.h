#pragma once

#include <cstdint>
#include <memory>

#include "envoy/http/codec.h"
#include "envoy/tcp/conn_pool.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/cleanup.h"
#include "common/common/logger.h"
#include "common/config/well_known_names.h"
#include "common/router/upstream_request.h"
#include "common/stream_info/stream_info_impl.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace Tcp {

class TcpConnPool : public Router::GenericConnPool, public Envoy::Tcp::ConnectionPool::Callbacks {
public:
  TcpConnPool(Upstream::ClusterManager& cm, bool is_connect, const Router::RouteEntry& route_entry,
              absl::optional<Envoy::Http::Protocol>, Upstream::LoadBalancerContext* ctx) {
    ASSERT(is_connect);
    // TODO(mattklein123): Pass thread local cluster into this function, removing an additional
    // map lookup and moving the error handling closer to the source (where it is likely already
    // done).
    const auto thread_local_cluster = cm.getThreadLocalCluster(route_entry.clusterName());
    if (thread_local_cluster != nullptr) {
      conn_pool_ = thread_local_cluster->tcpConnPool(Upstream::ResourcePriority::Default, ctx);
    }
  }
  void newStream(Router::GenericConnectionPoolCallbacks* callbacks) override {
    callbacks_ = callbacks;
    upstream_handle_ = conn_pool_->newConnection(*this);
  }

  bool cancelAnyPendingStream() override {
    if (upstream_handle_) {
      upstream_handle_->cancel(Envoy::Tcp::ConnectionPool::CancelPolicy::Default);
      upstream_handle_ = nullptr;
      return true;
    }
    return false;
  }
  Upstream::HostDescriptionConstSharedPtr host() const override { return conn_pool_->host(); }

  bool valid() { return conn_pool_ != nullptr; }

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     Upstream::HostDescriptionConstSharedPtr host) override {
    upstream_handle_ = nullptr;
    callbacks_->onPoolFailure(reason, "", host);
  }

  void onPoolReady(Envoy::Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Upstream::HostDescriptionConstSharedPtr host) override;

private:
  Envoy::Tcp::ConnectionPool::Instance* conn_pool_;
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
