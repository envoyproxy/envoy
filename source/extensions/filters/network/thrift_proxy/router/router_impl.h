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

#include "common/common/logger.h"
#include "common/http/header_utility.h"
#include "common/upstream/load_balancer_impl.h"

#include "extensions/filters/network/thrift_proxy/conn_manager.h"
#include "extensions/filters/network/thrift_proxy/filters/filter.h"
#include "extensions/filters/network/thrift_proxy/router/router.h"
#include "extensions/filters/network/thrift_proxy/router/router_ratelimit_impl.h"
#include "extensions/filters/network/thrift_proxy/thrift_object.h"

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

#define ALL_THRIFT_ROUTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                         \
  COUNTER(route_missing)                                                                           \
  COUNTER(unknown_cluster)                                                                         \
  COUNTER(upstream_rq_maintenance_mode)                                                            \
  COUNTER(no_healthy_upstream)

struct RouterStats {
  ALL_THRIFT_ROUTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

class Router : public Tcp::ConnectionPool::UpstreamCallbacks,
               public Upstream::LoadBalancerContextBase,
               public ProtocolConverter,
               public ThriftFilters::DecoderFilter,
               Logger::Loggable<Logger::Id::thrift> {
public:
  Router(Upstream::ClusterManager& cluster_manager, const std::string& stat_prefix,
         Stats::Scope& scope)
      : cluster_manager_(cluster_manager), stats_(generateStats(stat_prefix, scope)),
        stat_name_set_(scope.symbolTable().makeSet("thrift_proxy")),
        symbol_table_(scope.symbolTable()),
        upstream_rq_call_(stat_name_set_->add("thrift.upstream_rq_call")),
        upstream_rq_oneway_(stat_name_set_->add("thrift.upstream_rq_oneway")),
        upstream_rq_invalid_type_(stat_name_set_->add("thrift.upstream_rq_invalid_type")),
        upstream_resp_reply_(stat_name_set_->add("thrift.upstream_resp_reply")),
        upstream_resp_reply_success_(stat_name_set_->add("thrift.upstream_resp_success")),
        upstream_resp_reply_error_(stat_name_set_->add("thrift.upstream_resp_error")),
        upstream_resp_exception_(stat_name_set_->add("thrift.upstream_resp_exception")),
        upstream_resp_invalid_type_(stat_name_set_->add("thrift.upstream_resp_invalid_type")),
        upstream_rq_time_(stat_name_set_->add("thrift.upstream_rq_time")),
        passthrough_supported_(false) {}

  ~Router() override = default;

  // ThriftFilters::DecoderFilter
  void onDestroy() override;
  void setDecoderFilterCallbacks(ThriftFilters::DecoderFilterCallbacks& callbacks) override;
  bool passthroughSupported() const override { return passthrough_supported_; }

  // ProtocolConverter
  FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus transportEnd() override;
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus messageEnd() override;

  // Upstream::LoadBalancerContext
  const Network::Connection* downstreamConnection() const override;
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() override {
    if (route_entry_) {
      return route_entry_->metadataMatchCriteria();
    }
    return nullptr;
  }

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

private:
  struct UpstreamRequest : public Tcp::ConnectionPool::Callbacks {
    UpstreamRequest(Router& parent, Tcp::ConnectionPool::Instance& pool,
                    MessageMetadataSharedPtr& metadata, TransportType transport_type,
                    ProtocolType protocol_type);
    ~UpstreamRequest() override;

    FilterStatus start();
    void resetStream();
    void releaseConnection(bool close);

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
    void chargeResponseTiming();

    Router& parent_;
    Tcp::ConnectionPool::Instance& conn_pool_;
    MessageMetadataSharedPtr metadata_;

    Tcp::ConnectionPool::Cancellable* conn_pool_handle_{};
    Tcp::ConnectionPool::ConnectionDataPtr conn_data_;
    Upstream::HostDescriptionConstSharedPtr upstream_host_;
    ThriftConnectionState* conn_state_{};
    TransportPtr transport_;
    ProtocolPtr protocol_;
    ThriftObjectPtr upgrade_response_;

    bool request_complete_ : 1;
    bool response_started_ : 1;
    bool response_complete_ : 1;

    bool charged_response_timing_{false};
    MonotonicTime downstream_request_complete_time_;
  };

  // Stats
  void incClusterScopeCounter(const Stats::StatNameVec& names) const {
    const Stats::SymbolTable::StoragePtr stat_name_storage = symbol_table_.join(names);
    cluster_->statsScope().counterFromStatName(Stats::StatName(stat_name_storage.get())).inc();
  }

  void recordClusterScopeHistogram(const Stats::StatNameVec& names, Stats::Histogram::Unit unit,
                                   uint64_t count) const {
    const Stats::SymbolTable::StoragePtr stat_name_storage = symbol_table_.join(names);
    cluster_->statsScope()
        .histogramFromStatName(Stats::StatName(stat_name_storage.get()), unit)
        .recordValue(count);
  }

  void convertMessageBegin(MessageMetadataSharedPtr metadata);
  void cleanup();
  RouterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return RouterStats{ALL_THRIFT_ROUTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                               POOL_GAUGE_PREFIX(scope, prefix),
                                               POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }

  Upstream::ClusterManager& cluster_manager_;
  RouterStats stats_;
  Stats::StatNameSetPtr stat_name_set_;
  Stats::SymbolTable& symbol_table_;
  const Stats::StatName upstream_rq_call_;
  const Stats::StatName upstream_rq_oneway_;
  const Stats::StatName upstream_rq_invalid_type_;
  const Stats::StatName upstream_resp_reply_;
  const Stats::StatName upstream_resp_reply_success_;
  const Stats::StatName upstream_resp_reply_error_;
  const Stats::StatName upstream_resp_exception_;
  const Stats::StatName upstream_resp_invalid_type_;
  const Stats::StatName upstream_rq_time_;

  ThriftFilters::DecoderFilterCallbacks* callbacks_{};
  RouteConstSharedPtr route_{};
  const RouteEntry* route_entry_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;

  std::unique_ptr<UpstreamRequest> upstream_request_;
  Buffer::OwnedImpl upstream_request_buffer_;

  bool passthrough_supported_ : 1;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
