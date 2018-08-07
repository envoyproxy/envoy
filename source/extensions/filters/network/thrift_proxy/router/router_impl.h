#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/config/filter/network/thrift_proxy/v2alpha1/thrift_proxy.pb.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/load_balancer.h"

#include "common/common/logger.h"

#include "extensions/filters/network/thrift_proxy/conn_manager.h"
#include "extensions/filters/network/thrift_proxy/filters/filter.h"
#include "extensions/filters/network/thrift_proxy/router/router.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class RouteEntryImplBase : public RouteEntry,
                           public Route,
                           public std::enable_shared_from_this<RouteEntryImplBase> {
public:
  RouteEntryImplBase(const envoy::config::filter::network::thrift_proxy::v2alpha1::Route& route);

  // Router::RouteEntry
  const std::string& clusterName() const override;

  // Router::Route
  const RouteEntry* routeEntry() const override;

  virtual RouteConstSharedPtr matches(const MessageMetadata& metadata) const PURE;

protected:
  RouteConstSharedPtr clusterEntry() const;

private:
  const std::string cluster_name_;
};

typedef std::shared_ptr<const RouteEntryImplBase> RouteEntryImplBaseConstSharedPtr;

class MethodNameRouteEntryImpl : public RouteEntryImplBase {
public:
  MethodNameRouteEntryImpl(
      const envoy::config::filter::network::thrift_proxy::v2alpha1::Route& route);

  const std::string& methodName() const { return method_name_; }

  // RoutEntryImplBase
  RouteConstSharedPtr matches(const MessageMetadata& metadata) const override;

private:
  const std::string method_name_;
};

class RouteMatcher {
public:
  RouteMatcher(const envoy::config::filter::network::thrift_proxy::v2alpha1::RouteConfiguration&);

  RouteConstSharedPtr route(const MessageMetadata& metadata) const;

private:
  std::vector<RouteEntryImplBaseConstSharedPtr> routes_;
};

class Router : public Tcp::ConnectionPool::UpstreamCallbacks,
               public Upstream::LoadBalancerContext,
               public ProtocolConverter,
               Logger::Loggable<Logger::Id::thrift> {
public:
  Router(Upstream::ClusterManager& cluster_manager) : cluster_manager_(cluster_manager) {}

  ~Router() {}

  // ProtocolConverter
  void onDestroy() override;
  void setDecoderFilterCallbacks(ThriftFilters::DecoderFilterCallbacks& callbacks) override;
  void resetUpstreamConnection() override;
  ThriftFilters::FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;
  ThriftFilters::FilterStatus transportEnd() override;
  ThriftFilters::FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
  ThriftFilters::FilterStatus messageEnd() override;

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return {}; }
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() override { return nullptr; }
  const Network::Connection* downstreamConnection() const override;
  const Http::HeaderMap* downstreamHeaders() const override { return nullptr; }

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  struct UpstreamRequest : public Tcp::ConnectionPool::Callbacks {
    UpstreamRequest(Router& parent, Tcp::ConnectionPool::Instance& pool,
                    MessageMetadataSharedPtr& metadata);
    ~UpstreamRequest();

    void start();
    void resetStream();

    // Tcp::ConnectionPool::Callbacks
    void onPoolFailure(Tcp::ConnectionPool::PoolFailureReason reason,
                       Upstream::HostDescriptionConstSharedPtr host) override;
    void onPoolReady(Tcp::ConnectionPool::ConnectionData& conn,
                     Upstream::HostDescriptionConstSharedPtr host) override;

    void onRequestComplete();
    void onResponseComplete();
    void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);
    void onResetStream(Tcp::ConnectionPool::PoolFailureReason reason);

    Router& parent_;
    Tcp::ConnectionPool::Instance& conn_pool_;
    MessageMetadataSharedPtr metadata_;

    Tcp::ConnectionPool::Cancellable* conn_pool_handle_{};
    Tcp::ConnectionPool::ConnectionData* conn_data_{};
    Upstream::HostDescriptionConstSharedPtr upstream_host_;
    TransportPtr transport_;
    ProtocolType proto_type_{ProtocolType::Auto};

    bool request_complete_ : 1;
    bool response_started_ : 1;
    bool response_complete_ : 1;
  };

  void convertMessageBegin(MessageMetadataSharedPtr metadata);
  void cleanup();

  Upstream::ClusterManager& cluster_manager_;

  ThriftFilters::DecoderFilterCallbacks* callbacks_{};
  RouteConstSharedPtr route_{};
  const RouteEntry* route_entry_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;

  std::unique_ptr<UpstreamRequest> upstream_request_;
  Buffer::OwnedImpl upstream_request_buffer_;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
