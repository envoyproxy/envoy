#pragma once

#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/tcp/conn_pool.h"

#include "common/common/logger.h"
#include "common/upstream/load_balancer_impl.h"

#include "extensions/filters/network/dubbo_proxy/filters/filter.h"
#include "extensions/filters/network/dubbo_proxy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {
namespace Router {

class Router : public Tcp::ConnectionPool::UpstreamCallbacks,
               public Upstream::LoadBalancerContextBase,
               public DubboFilters::DecoderFilter,
               Logger::Loggable<Logger::Id::dubbo> {
public:
  Router(Upstream::ClusterManager& cluster_manager) : cluster_manager_(cluster_manager) {}
  ~Router() override = default;

  // DubboFilters::DecoderFilter
  void onDestroy() override;
  void setDecoderFilterCallbacks(DubboFilters::DecoderFilterCallbacks& callbacks) override;

  FilterStatus onMessageDecoded(MessageMetadataSharedPtr metadata, ContextSharedPtr ctx) override;

  // Upstream::LoadBalancerContextBase
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() override { return nullptr; }
  const Network::Connection* downstreamConnection() const override;

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  struct UpstreamRequest : public Tcp::ConnectionPool::Callbacks {
    UpstreamRequest(Router& parent, Tcp::ConnectionPool::Instance& pool,
                    MessageMetadataSharedPtr& metadata, SerializationType serialization_type,
                    ProtocolType protocol_type);
    ~UpstreamRequest() override;

    FilterStatus start();
    void resetStream();
    void encodeData(Buffer::Instance& data);

    // Tcp::ConnectionPool::Callbacks
    void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                       Upstream::HostDescriptionConstSharedPtr host) override;
    void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                     Upstream::HostDescriptionConstSharedPtr host) override;

    void onRequestStart(bool continue_decoding);
    void onRequestComplete();
    void onResponseComplete();
    void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);
    void onResetStream(ConnectionPool::PoolFailureReason reason);

    Router& parent_;
    Tcp::ConnectionPool::Instance& conn_pool_;
    MessageMetadataSharedPtr metadata_;

    Tcp::ConnectionPool::Cancellable* conn_pool_handle_{};
    Tcp::ConnectionPool::ConnectionDataPtr conn_data_;
    Upstream::HostDescriptionConstSharedPtr upstream_host_;
    SerializerPtr serializer_;
    ProtocolPtr protocol_;

    bool request_complete_ : 1;
    bool response_started_ : 1;
    bool response_complete_ : 1;
    bool stream_reset_ : 1;
  };

  void cleanup();

  Upstream::ClusterManager& cluster_manager_;

  DubboFilters::DecoderFilterCallbacks* callbacks_{};
  RouteConstSharedPtr route_{};
  const RouteEntry* route_entry_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;

  std::unique_ptr<UpstreamRequest> upstream_request_;
  Envoy::Buffer::OwnedImpl upstream_request_buffer_;

  bool filter_complete_{false};
};

} // namespace Router
} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
