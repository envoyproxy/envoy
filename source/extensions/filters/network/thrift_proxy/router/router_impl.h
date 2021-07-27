#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.h"
#include "envoy/router/router.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/common/logger.h"
#include "source/common/http/header_utility.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/extensions/filters/network/thrift_proxy/conn_manager.h"
#include "source/extensions/filters/network/thrift_proxy/filters/filter.h"
#include "source/extensions/filters/network/thrift_proxy/router/router.h"
#include "source/extensions/filters/network/thrift_proxy/router/router_ratelimit_impl.h"
#include "source/extensions/filters/network/thrift_proxy/router/upstream_request.h"
#include "source/extensions/filters/network/thrift_proxy/thrift_object.h"

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
  RouteEntryImplBase(const envoy::extensions::filters::network::thrift_proxy::v3::Route& route);

  // Router::RouteEntry
  const std::string& clusterName() const override;
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
    return metadata_match_criteria_.get();
  }
  const RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
  bool stripServiceName() const override { return strip_service_name_; };
  const Http::LowerCaseString& clusterHeader() const override { return cluster_header_; }

  // Router::Route
  const RouteEntry* routeEntry() const override;

  virtual RouteConstSharedPtr matches(const MessageMetadata& metadata,
                                      uint64_t random_value) const PURE;

protected:
  RouteConstSharedPtr clusterEntry(uint64_t random_value, const MessageMetadata& metadata) const;
  bool headersMatch(const Http::HeaderMap& headers) const;

private:
  class WeightedClusterEntry : public RouteEntry, public Route {
  public:
    WeightedClusterEntry(
        const RouteEntryImplBase& parent,
        const envoy::extensions::filters::network::thrift_proxy::v3::WeightedCluster::ClusterWeight&
            cluster);

    uint64_t clusterWeight() const { return cluster_weight_; }

    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }
    const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
      if (metadata_match_criteria_) {
        return metadata_match_criteria_.get();
      }

      return parent_.metadataMatchCriteria();
    }
    const RateLimitPolicy& rateLimitPolicy() const override { return parent_.rateLimitPolicy(); }
    bool stripServiceName() const override { return parent_.stripServiceName(); }
    const Http::LowerCaseString& clusterHeader() const override { return parent_.clusterHeader(); }

    // Router::Route
    const RouteEntry* routeEntry() const override { return this; }

  private:
    const RouteEntryImplBase& parent_;
    const std::string cluster_name_;
    const uint64_t cluster_weight_;
    Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria_;
  };
  using WeightedClusterEntrySharedPtr = std::shared_ptr<WeightedClusterEntry>;

  class DynamicRouteEntry : public RouteEntry, public Route {
  public:
    DynamicRouteEntry(const RouteEntryImplBase& parent, absl::string_view cluster_name)
        : parent_(parent), cluster_name_(std::string(cluster_name)) {}

    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }
    const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
      return parent_.metadataMatchCriteria();
    }
    const RateLimitPolicy& rateLimitPolicy() const override { return parent_.rateLimitPolicy(); }
    bool stripServiceName() const override { return parent_.stripServiceName(); }
    const Http::LowerCaseString& clusterHeader() const override { return parent_.clusterHeader(); }

    // Router::Route
    const RouteEntry* routeEntry() const override { return this; }

  private:
    const RouteEntryImplBase& parent_;
    const std::string cluster_name_;
  };

  const std::string cluster_name_;
  const std::vector<Http::HeaderUtility::HeaderDataPtr> config_headers_;
  std::vector<WeightedClusterEntrySharedPtr> weighted_clusters_;
  uint64_t total_cluster_weight_;
  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria_;
  const RateLimitPolicyImpl rate_limit_policy_;
  const bool strip_service_name_;
  const Http::LowerCaseString cluster_header_;
};

using RouteEntryImplBaseConstSharedPtr = std::shared_ptr<const RouteEntryImplBase>;

class MethodNameRouteEntryImpl : public RouteEntryImplBase {
public:
  MethodNameRouteEntryImpl(
      const envoy::extensions::filters::network::thrift_proxy::v3::Route& route);

  // RouteEntryImplBase
  RouteConstSharedPtr matches(const MessageMetadata& metadata,
                              uint64_t random_value) const override;

private:
  const std::string method_name_;
  const bool invert_;
};

class ServiceNameRouteEntryImpl : public RouteEntryImplBase {
public:
  ServiceNameRouteEntryImpl(
      const envoy::extensions::filters::network::thrift_proxy::v3::Route& route);

  // RouteEntryImplBase
  RouteConstSharedPtr matches(const MessageMetadata& metadata,
                              uint64_t random_value) const override;

private:
  std::string service_name_;
  const bool invert_;
};

class RouteMatcher {
public:
  RouteMatcher(const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration&);

  RouteConstSharedPtr route(const MessageMetadata& metadata, uint64_t random_value) const;

private:
  std::vector<RouteEntryImplBaseConstSharedPtr> routes_;
};

class Router : public Tcp::ConnectionPool::UpstreamCallbacks,
               public Upstream::LoadBalancerContextBase,
               public RequestOwner,
               public ThriftFilters::DecoderFilter,
               Logger::Loggable<Logger::Id::thrift> {
public:
  Router(Upstream::ClusterManager& cluster_manager, const std::string& stat_prefix,
         Stats::Scope& scope)
      : RequestOwner(cluster_manager, stat_prefix, scope), passthrough_supported_(false) {}

  ~Router() override = default;

  // ThriftFilters::DecoderFilter
  void onDestroy() override;
  void setDecoderFilterCallbacks(ThriftFilters::DecoderFilterCallbacks& callbacks) override;
  bool passthroughSupported() const override { return passthrough_supported_; }

  // RequestOwner
  Tcp::ConnectionPool::UpstreamCallbacks& upstreamCallbacks() override { return *this; }
  Buffer::OwnedImpl& buffer() override { return upstream_request_buffer_; }
  Event::Dispatcher& dispatcher() override { return callbacks_->dispatcher(); }
  void addSize(uint64_t size) override { request_size_ += size; }
  void continueDecoding() override { callbacks_->continueDecoding(); }
  void resetDownstreamConnection() override { callbacks_->resetDownstreamConnection(); }
  void sendLocalReply(const ThriftProxy::DirectResponse& response, bool end_stream) override {
    callbacks_->sendLocalReply(response, end_stream);
  }
  void recordResponseDuration(uint64_t value, Stats::Histogram::Unit unit) override {
    recordClusterResponseDuration(*cluster_, value, unit);
  }

  // RequestOwner::ProtocolConverter
  FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus transportEnd() override;
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus messageEnd() override;

  // Upstream::LoadBalancerContext
  const Network::Connection* downstreamConnection() const override;
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() override {
    return route_entry_ ? route_entry_->metadataMatchCriteria() : nullptr;
  }

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  void cleanup();

  ThriftFilters::DecoderFilterCallbacks* callbacks_{};
  RouteConstSharedPtr route_{};
  const RouteEntry* route_entry_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;

  std::unique_ptr<UpstreamRequest> upstream_request_;
  Buffer::OwnedImpl upstream_request_buffer_;

  bool passthrough_supported_ : 1;
  uint64_t request_size_{};
  uint64_t response_size_{};
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
