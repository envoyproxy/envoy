#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/router/router.h"
#include "envoy/tcp/conn_pool.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "source/extensions/filters/network/thrift_proxy/metadata.h"
#include "source/extensions/filters/network/thrift_proxy/protocol_converter.h"
#include "source/extensions/filters/network/thrift_proxy/protocol_options_config.h"
#include "source/extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class RateLimitPolicy;
class RequestMirrorPolicy;

/**
 * RouteEntry is an individual resolved route entry.
 */
class RouteEntry {
public:
  virtual ~RouteEntry() = default;

  /**
   * @return const std::string& the upstream cluster that owns the route.
   */
  virtual const std::string& clusterName() const PURE;

  /**
   * @return MetadataMatchCriteria* the metadata that a subset load balancer should match when
   * selecting an upstream host
   */
  virtual const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const PURE;

  /**
   * @return const RateLimitPolicy& the rate limit policy for the route.
   */
  virtual const RateLimitPolicy& rateLimitPolicy() const PURE;

  /**
   * @return bool should the service name prefix be stripped from the method.
   */
  virtual bool stripServiceName() const PURE;

  /**
   * @return const Http::LowerCaseString& the header used to determine the cluster.
   */
  virtual const Http::LowerCaseString& clusterHeader() const PURE;

  /**
   * @return const std::vector<RequestMirrorPolicy>& the mirror policies associated with this route,
   * if any.
   */
  virtual const std::vector<std::shared_ptr<RequestMirrorPolicy>>&
  requestMirrorPolicies() const PURE;
};

/**
 * Route holds the RouteEntry for a request.
 */
class Route {
public:
  virtual ~Route() = default;

  /**
   * @return the route entry or nullptr if there is no matching route for the request.
   */
  virtual const RouteEntry* routeEntry() const PURE;
};

using RouteConstSharedPtr = std::shared_ptr<const Route>;

/**
 * The router configuration.
 */
class Config {
public:
  virtual ~Config() = default;

  /**
   * Based on the incoming Thrift request transport and/or protocol data, determine the target
   * route for the request.
   * @param metadata MessageMetadata for the message to route
   * @param random_value uint64_t used to select cluster affinity
   * @return the route or nullptr if there is no matching route for the request.
   */
  virtual RouteConstSharedPtr route(const MessageMetadata& metadata,
                                    uint64_t random_value) const PURE;
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

#define ALL_THRIFT_ROUTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                         \
  COUNTER(route_missing)                                                                           \
  COUNTER(unknown_cluster)                                                                         \
  COUNTER(upstream_rq_maintenance_mode)                                                            \
  COUNTER(no_healthy_upstream)

struct RouterStats {
  ALL_THRIFT_ROUTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

/**
 * This interface is used by an upstream request to communicate its state.
 */
class RequestOwner : public ProtocolConverter, public Logger::Loggable<Logger::Id::thrift> {
public:
  RequestOwner(Upstream::ClusterManager& cluster_manager, const std::string& stat_prefix,
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
        upstream_rq_size_(stat_name_set_->add("thrift.upstream_rq_size")),
        upstream_resp_size_(stat_name_set_->add("thrift.upstream_resp_size")) {}
  ~RequestOwner() override = default;

  /**
   * @return ConnectionPool::UpstreamCallbacks& the handler for upstream data.
   */
  virtual Tcp::ConnectionPool::UpstreamCallbacks& upstreamCallbacks() PURE;

  /**
   * @return Buffer::OwnedImpl& the buffer used to serialize the upstream request.
   */
  virtual Buffer::OwnedImpl& buffer() PURE;

  /**
   * @return Event::Dispatcher& the dispatcher used for timers, etc.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * Converts message begin into the right protocol.
   */
  void convertMessageBegin(MessageMetadataSharedPtr metadata) {
    ProtocolConverter::messageBegin(metadata);
  }

  /**
   * Used to update the request size every time bytes are pushed out.
   *
   * @param size uint64_t the value of the increment.
   */
  virtual void addSize(uint64_t size) PURE;

  /**
   * Used to continue decoding if it was previously stopped.
   */
  virtual void continueDecoding() PURE;

  /**
   * Used to reset the downstream connection after an error.
   */
  virtual void resetDownstreamConnection() PURE;

  /**
   * Sends a locally generated response using the provided response object.
   *
   * @param response DirectResponse the response to send to the downstream client
   * @param end_stream if true, the downstream connection should be closed after this response
   */
  virtual void sendLocalReply(const ThriftProxy::DirectResponse& response, bool end_stream) PURE;

  /**
   * Records the duration of the request.
   *
   * @param value uint64_t the value of the duration.
   * @param unit Unit the unit of the duration.
   */
  virtual void recordResponseDuration(uint64_t value, Stats::Histogram::Unit unit) PURE;

  /**
   * @return Upstream::ClusterManager& the cluster manager.
   */
  Upstream::ClusterManager& clusterManager() { return cluster_manager_; }

  /**
   * @return Upstream::Cluster& the upstream cluster associated with the request.
   */
  const Upstream::ClusterInfo& cluster() const { return *cluster_; }

  /**
   * Common stats.
   */
  RouterStats& stats() { return stats_; }

  /**
   * Increment counter for received responses that are replies.
   */
  void incResponseReply(const Upstream::ClusterInfo& cluster) {
    incClusterScopeCounter(cluster, {upstream_resp_reply_});
  }

  /**
   * Increment counter for request calls.
   */
  void incRequestCall(const Upstream::ClusterInfo& cluster) {
    incClusterScopeCounter(cluster, {upstream_rq_call_});
  }

  /**
   * Increment counter for requests that are one way only.
   */
  void incRequestOneWay(const Upstream::ClusterInfo& cluster) {
    incClusterScopeCounter(cluster, {upstream_rq_oneway_});
  }

  /**
   * Increment counter for requests that are invalid.
   */
  void incRequestInvalid(const Upstream::ClusterInfo& cluster) {
    incClusterScopeCounter(cluster, {upstream_rq_invalid_type_});
  }

  /**
   * Increment counter for received responses that are replies that are successful.
   */
  void incResponseReplySuccess(const Upstream::ClusterInfo& cluster) {
    incClusterScopeCounter(cluster, {upstream_resp_reply_success_});
  }

  /**
   * Increment counter for received responses that are replies that are an error.
   */
  void incResponseReplyError(const Upstream::ClusterInfo& cluster) {
    incClusterScopeCounter(cluster, {upstream_resp_reply_error_});
  }

  /**
   * Increment counter for received responses that are exceptions.
   */
  void incResponseException(const Upstream::ClusterInfo& cluster) {
    incClusterScopeCounter(cluster, {upstream_resp_exception_});
  }

  /**
   * Increment counter for received responses that are invalid.
   */
  void incResponseInvalidType(const Upstream::ClusterInfo& cluster) {
    incClusterScopeCounter(cluster, {upstream_resp_invalid_type_});
  }

  /**
   * Record a value for the request size histogram.
   */
  void recordUpstreamRequestSize(const Upstream::ClusterInfo& cluster, uint64_t value) {
    recordClusterScopeHistogram(cluster, {upstream_rq_size_}, Stats::Histogram::Unit::Bytes, value);
  }

  /**
   * Record a value for the response size histogram.
   */
  void recordUpstreamResponseSize(const Upstream::ClusterInfo& cluster, uint64_t value) {
    recordClusterScopeHistogram(cluster, {upstream_resp_size_}, Stats::Histogram::Unit::Bytes,
                                value);
  }

  /**
   * Records the duration of the request for a given cluster.
   *
   * @param cluster ClusterInfo the cluster to record the duration for.
   * @param value uint64_t the value of the duration.
   * @param unit Unit the unit of the duration.
   */
  void recordClusterResponseDuration(const Upstream::ClusterInfo& cluster, uint64_t value,
                                     Stats::Histogram::Unit unit) {
    recordClusterScopeHistogram(cluster, {upstream_rq_time_}, unit, value);
  }

protected:
  struct UpstreamRequestInfo {
    bool passthrough_supported;
    TransportType transport;
    ProtocolType protocol;
    absl::optional<Upstream::TcpPoolData> conn_pool_data;
  };

  struct PrepareUpstreamRequestResult {
    absl::optional<AppException> exception;
    absl::optional<UpstreamRequestInfo> upstream_request_info;
  };

  PrepareUpstreamRequestResult prepareUpstreamRequest(const std::string& cluster_name,
                                                      MessageMetadataSharedPtr& metadata,
                                                      TransportType transport,
                                                      ProtocolType protocol,
                                                      Upstream::LoadBalancerContext* lb_context) {
    Upstream::ThreadLocalCluster* cluster = clusterManager().getThreadLocalCluster(cluster_name);
    if (!cluster) {
      ENVOY_LOG(debug, "unknown cluster '{}'", cluster_name);
      stats().unknown_cluster_.inc();
      return {AppException(AppExceptionType::InternalError,
                           fmt::format("unknown cluster '{}'", cluster_name)),
              absl::nullopt};
    }

    cluster_ = cluster->info();
    ENVOY_LOG(debug, "cluster '{}' match for method '{}'", cluster_name, metadata->methodName());

    switch (metadata->messageType()) {
    case MessageType::Call:
      incRequestCall(*cluster_);
      break;

    case MessageType::Oneway:
      incRequestOneWay(*cluster_);
      break;

    default:
      incRequestInvalid(*cluster_);
      break;
    }

    if (cluster_->maintenanceMode()) {
      stats().upstream_rq_maintenance_mode_.inc();
      return {AppException(AppExceptionType::InternalError,
                           fmt::format("maintenance mode for cluster '{}'", cluster_name)),
              absl::nullopt};
    }

    const std::shared_ptr<const ProtocolOptionsConfig> options =
        cluster_->extensionProtocolOptionsTyped<ProtocolOptionsConfig>(
            NetworkFilterNames::get().ThriftProxy);

    const TransportType final_transport = options ? options->transport(transport) : transport;
    ASSERT(final_transport != TransportType::Auto);

    const ProtocolType final_protocol = options ? options->protocol(protocol) : protocol;
    ASSERT(final_protocol != ProtocolType::Auto);

    auto conn_pool_data = cluster->tcpConnPool(Upstream::ResourcePriority::Default, lb_context);
    if (!conn_pool_data) {
      stats().no_healthy_upstream_.inc();
      return {AppException(AppExceptionType::InternalError,
                           fmt::format("no healthy upstream for '{}'", cluster_name)),
              absl::nullopt};
    }

    const auto passthrough_supported =
        transport == TransportType::Framed && final_transport == TransportType::Framed &&
        protocol == final_protocol && final_protocol != ProtocolType::Twitter;
    UpstreamRequestInfo result = {passthrough_supported, final_transport, final_protocol,
                                  conn_pool_data};
    return {absl::nullopt, result};
  }

  Upstream::ClusterInfoConstSharedPtr cluster_;

private:
  void incClusterScopeCounter(const Upstream::ClusterInfo& cluster,
                              const Stats::StatNameVec& names) const {
    const Stats::SymbolTable::StoragePtr stat_name_storage = symbol_table_.join(names);
    cluster.statsScope().counterFromStatName(Stats::StatName(stat_name_storage.get())).inc();
  }

  void recordClusterScopeHistogram(const Upstream::ClusterInfo& cluster,
                                   const Stats::StatNameVec& names, Stats::Histogram::Unit unit,
                                   uint64_t value) const {
    const Stats::SymbolTable::StoragePtr stat_name_storage = symbol_table_.join(names);
    cluster.statsScope()
        .histogramFromStatName(Stats::StatName(stat_name_storage.get()), unit)
        .recordValue(value);
  }

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
  const Stats::StatName upstream_rq_size_;
  const Stats::StatName upstream_resp_size_;
};

/**
 * RequestMirrorPolicy is an individual mirroring rule for a route entry.
 */
class RequestMirrorPolicy {
public:
  virtual ~RequestMirrorPolicy() = default;

  /**
   * @return const std::string& the upstream cluster that should be used for the mirrored request.
   */
  virtual const std::string& clusterName() const PURE;

  /**
   * @return bool whether this policy is currently enabled.
   */
  virtual bool enabled(Runtime::Loader& runtime) const PURE;
};

/**
 * ShadowRouterHandle is used to write a request or release a connection early if needed.
 */
class ShadowRouterHandle {
public:
  virtual ~ShadowRouterHandle() = default;

  /**
   * Called after the Router is destroyed.
   */
  virtual void onRouterDestroy() PURE;

  /**
   * Checks if the request is currently waiting for an upstream connection to become available.
   */
  virtual bool waitingForConnection() const PURE;

  /**
   * @return RequestOwner& the interface associated with this ShadowRouter.
   */
  virtual RequestOwner& requestOwner() PURE;
};

/**
 * ShadowWriter is used for submitting requests and ignoring the response.
 */
class ShadowWriter {
public:
  virtual ~ShadowWriter() = default;

  /**
   * @return Upstream::ClusterManager& the cluster manager.
   */
  virtual Upstream::ClusterManager& clusterManager() PURE;

  /**
   * @return std::string& the stat prefix used by the router.
   */
  virtual const std::string& statPrefix() const PURE;

  /**
   * @return Stats::Scope& the Scope used by the router.
   */
  virtual Stats::Scope& scope() PURE;

  /**
   * @return Dispatcher& the dispatcher.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * Starts the shadow request by requesting an upstream connection.
   */
  virtual absl::optional<std::reference_wrapper<ShadowRouterHandle>>
  submit(const std::string& cluster_name, MessageMetadataSharedPtr metadata,
         TransportType original_transport, ProtocolType original_protocol) PURE;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
