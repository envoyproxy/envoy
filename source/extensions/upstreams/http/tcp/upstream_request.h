#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/http/codec.h"
#include "envoy/http/codes.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/tcp/conn_pool.h"

#include "common/buffer/watermark_buffer.h"
#include "common/common/cleanup.h"
#include "common/common/hash.h"
#include "common/common/hex.h"
#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/config/well_known_names.h"
#include "common/router/upstream_request.h"
#include "common/stream_info/stream_info_impl.h"

namespace Envoy {
namespace Router {

class GenericUpstream;
class GenericConnectionPoolCallbacks;
class RouterFilterInterface;
class UpstreamRequest;

class TcpConnPool : public GenericConnPool, public Tcp::ConnectionPool::Callbacks {
public:
  bool initialize(Upstream::ClusterManager& cm, const Router::RouteEntry& route_entry, Envoy::Http::Protocol,
                  Upstream::LoadBalancerContext* ctx) override {
    conn_pool_ = cm.tcpConnPoolForCluster(route_entry.clusterName(),
                                          Upstream::ResourcePriority::Default, ctx);
    return conn_pool_ != nullptr;
  }

  void newStream(GenericConnectionPoolCallbacks* callbacks) override {
    callbacks_ = callbacks;
    upstream_handle_ = conn_pool_->newConnection(*this);
  }

  bool cancelAnyPendingRequest() override {
    if (upstream_handle_) {
      upstream_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
      upstream_handle_ = nullptr;
      return true;
    }
    return false;
  }
  absl::optional<Http::Protocol> protocol() const override { return absl::nullopt; }
  Upstream::HostDescriptionConstSharedPtr host() const override { return conn_pool_->host(); }

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     Upstream::HostDescriptionConstSharedPtr host) override {
    upstream_handle_ = nullptr;
    callbacks_->onPoolFailure(reason, "", host);
  }

  void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                   Upstream::HostDescriptionConstSharedPtr host) override;

private:
  Tcp::ConnectionPool::Instance* conn_pool_;
  Tcp::ConnectionPool::Cancellable* upstream_handle_{};
  GenericConnectionPoolCallbacks* callbacks_{};
};

class TcpUpstream : public GenericUpstream, public Tcp::ConnectionPool::UpstreamCallbacks {
public:
  TcpUpstream(UpstreamRequest* upstream_request, Tcp::ConnectionPool::ConnectionDataPtr&& upstream);

  // GenericUpstream
  void encodeData(Buffer::Instance& data, bool end_stream) override;
  void encodeMetadata(const Http::MetadataMapVector&) override {}
  void encodeHeaders(const Http::RequestHeaderMap&, bool end_stream) override;
  void encodeTrailers(const Http::RequestTrailerMap&) override;
  void readDisable(bool disable) override;
  void resetStream() override;

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override;
  void onBelowWriteBufferLowWatermark() override;

private:
  UpstreamRequest* upstream_request_;
  Tcp::ConnectionPool::ConnectionDataPtr upstream_conn_data_;
};

} // namespace Router
} // namespace Envoy
