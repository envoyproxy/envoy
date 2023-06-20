#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/local_info/local_info.h"
#include "envoy/rds/config.h"
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
class Config : public Rds::Config {
public:
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
  COUNTER(no_healthy_upstream)                                                                     \
  COUNTER(shadow_request_submit_failure)

/**
 * Struct containing named stats for the router.
 */
struct RouterNamedStats {
  ALL_THRIFT_ROUTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)

  static RouterNamedStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return RouterNamedStats{ALL_THRIFT_ROUTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                                    POOL_GAUGE_PREFIX(scope, prefix),
                                                    POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }
};

/**
 * Stats for use in the router.
 */
class RouterStats {
public:
  RouterStats(const std::string& stat_prefix, Stats::Scope& scope,
              const LocalInfo::LocalInfo& local_info)
      : named_(RouterNamedStats::generateStats(stat_prefix, scope)),
        stat_name_set_(scope.symbolTable().makeSet("thrift_proxy")),
        symbol_table_(scope.symbolTable()),
        upstream_rq_call_(stat_name_set_->add("thrift.upstream_rq_call")),
        upstream_rq_oneway_(stat_name_set_->add("thrift.upstream_rq_oneway")),
        upstream_rq_invalid_type_(stat_name_set_->add("thrift.upstream_rq_invalid_type")),
        upstream_resp_reply_(stat_name_set_->add("thrift.upstream_resp_reply")),
        upstream_resp_reply_success_(stat_name_set_->add("thrift.upstream_resp_success")),
        upstream_resp_reply_error_(stat_name_set_->add("thrift.upstream_resp_error")),
        upstream_resp_exception_(stat_name_set_->add("thrift.upstream_resp_exception")),
        upstream_resp_exception_local_(stat_name_set_->add("thrift.upstream_resp_exception_local")),
        upstream_resp_exception_remote_(
            stat_name_set_->add("thrift.upstream_resp_exception_remote")),
        upstream_resp_invalid_type_(stat_name_set_->add("thrift.upstream_resp_invalid_type")),
        upstream_resp_decoding_error_(stat_name_set_->add("thrift.upstream_resp_decoding_error")),
        upstream_rq_time_(stat_name_set_->add("thrift.upstream_rq_time")),
        upstream_rq_size_(stat_name_set_->add("thrift.upstream_rq_size")),
        upstream_resp_size_(stat_name_set_->add("thrift.upstream_resp_size")),
        zone_(stat_name_set_->add("zone")), local_zone_name_(local_info.zoneStatName()),
        upstream_cx_drain_close_(stat_name_set_->add("thrift.upstream_cx_drain_close")),
        downstream_cx_partial_response_close_(
            stat_name_set_->add("thrift.downstream_cx_partial_response_close")),
        downstream_cx_underflow_response_close_(
            stat_name_set_->add("thrift.downstream_cx_underflow_response_close")) {}

  /**
   * Increment counter for request calls.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   */
  void incRequestCall(const Upstream::ClusterInfo& cluster) const {
    incClusterScopeCounter(cluster, nullptr, upstream_rq_call_);
  }

  /**
   * Increment counter for requests that are one way only.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   */
  void incRequestOneWay(const Upstream::ClusterInfo& cluster) const {
    incClusterScopeCounter(cluster, nullptr, upstream_rq_oneway_);
  }

  /**
   * Increment counter for requests that are invalid.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   */
  void incRequestInvalid(const Upstream::ClusterInfo& cluster) const {
    incClusterScopeCounter(cluster, nullptr, upstream_rq_invalid_type_);
  }

  /**
   * Increment counter for connections that were closed due to draining.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   */
  void incCloseDrain(const Upstream::ClusterInfo& cluster) const {
    incClusterScopeCounter(cluster, nullptr, upstream_cx_drain_close_);
  }

  /**
   * Increment counter for downstream connections that were closed due to partial responses.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   */
  void incClosePartialResponse(const Upstream::ClusterInfo& cluster) const {
    incClusterScopeCounter(cluster, nullptr, downstream_cx_partial_response_close_);
  }

  /**
   * Increment counter for downstream connections that were closed due to underflow responses.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   */
  void incCloseUnderflowResponse(const Upstream::ClusterInfo& cluster) const {
    incClusterScopeCounter(cluster, nullptr, downstream_cx_underflow_response_close_);
  }

  /**
   * Increment counter for received responses that are replies that are successful.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   * @param upstream_host Upstream::HostDescriptionConstSharedPtr describing the upstream host
   */
  void incResponseReplySuccess(const Upstream::ClusterInfo& cluster,
                               Upstream::HostDescriptionConstSharedPtr upstream_host) const {
    incClusterScopeCounter(cluster, upstream_host, upstream_resp_reply_);
    incClusterScopeCounter(cluster, upstream_host, upstream_resp_reply_success_);
    ASSERT(upstream_host != nullptr);
    upstream_host->stats().rq_success_.inc();
  }

  /**
   * Increment counter for received responses that are replies that are an error.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   * @param upstream_host Upstream::HostDescriptionConstSharedPtr describing the upstream host
   */
  void incResponseReplyError(const Upstream::ClusterInfo& cluster,
                             Upstream::HostDescriptionConstSharedPtr upstream_host) const {
    incClusterScopeCounter(cluster, upstream_host, upstream_resp_reply_);
    incClusterScopeCounter(cluster, upstream_host, upstream_resp_reply_error_);
    ASSERT(upstream_host != nullptr);
    // Currently IDL exceptions are always considered endpoint error but it's possible for an error
    // to have semantics matching HTTP 4xx, rather than 5xx. rq_error classification chosen
    // here to match outlier detection external failure in upstream_request.cc.
    upstream_host->stats().rq_error_.inc();
  }

  /**
   * Increment counter for received remote responses that are exceptions.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   * @param upstream_host Upstream::HostDescriptionConstSharedPtr describing the upstream host
   */
  void incResponseRemoteException(const Upstream::ClusterInfo& cluster,
                                  Upstream::HostDescriptionConstSharedPtr upstream_host) const {
    incClusterScopeCounter(cluster, upstream_host, upstream_resp_exception_);
    ASSERT(upstream_host != nullptr);
    incClusterScopeCounter(cluster, nullptr, upstream_resp_exception_remote_);
    upstream_host->stats().rq_error_.inc();
  }

  /**
   * Increment counter for responses that are local exceptions, without forwarding a request
   * upstream.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   */
  void incResponseLocalException(const Upstream::ClusterInfo& cluster) const {
    incClusterScopeCounter(cluster, nullptr, upstream_resp_exception_);
    incClusterScopeCounter(cluster, nullptr, upstream_resp_exception_local_);
  }

  /**
   * Increment counter for received responses that are invalid.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   * @param upstream_host Upstream::HostDescriptionConstSharedPtr describing the upstream host
   */
  void incResponseInvalidType(const Upstream::ClusterInfo& cluster,
                              Upstream::HostDescriptionConstSharedPtr upstream_host) const {
    incClusterScopeCounter(cluster, upstream_host, upstream_resp_invalid_type_);
    ASSERT(upstream_host != nullptr);
    upstream_host->stats().rq_error_.inc();
  }

  /**
   * Increment counter for decoding errors during responses.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   * @param upstream_host Upstream::HostDescriptionConstSharedPtr describing the upstream host
   */
  void incResponseDecodingError(const Upstream::ClusterInfo& cluster,
                                Upstream::HostDescriptionConstSharedPtr upstream_host) const {
    incClusterScopeCounter(cluster, upstream_host, upstream_resp_decoding_error_);
    ASSERT(upstream_host != nullptr);
    upstream_host->stats().rq_error_.inc();
  }

  /**
   * Record a value for the request size histogram.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   * @param value uint64_t size in bytes of the full request
   */
  void recordUpstreamRequestSize(const Upstream::ClusterInfo& cluster, uint64_t value) const {
    recordClusterScopeHistogram(cluster, nullptr, upstream_rq_size_, Stats::Histogram::Unit::Bytes,
                                value);
  }

  /**
   * Record a value for the response size histogram.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   * @param value uint64_t size in bytes of the full response
   */
  void recordUpstreamResponseSize(const Upstream::ClusterInfo& cluster, uint64_t value) const {
    recordClusterScopeHistogram(cluster, nullptr, upstream_resp_size_,
                                Stats::Histogram::Unit::Bytes, value);
  }

  /**
   * Record a value for the response time duration histogram.
   * @param cluster Upstream::ClusterInfo& describing the upstream cluster
   * @param upstream_host Upstream::HostDescriptionConstSharedPtr describing the upstream host
   * @param value uint64_t duration in milliseconds to receive the complete response
   */
  void recordUpstreamResponseTime(const Upstream::ClusterInfo& cluster,
                                  Upstream::HostDescriptionConstSharedPtr upstream_host,
                                  uint64_t value) const {
    recordClusterScopeHistogram(cluster, upstream_host, upstream_rq_time_,
                                Stats::Histogram::Unit::Milliseconds, value);
  }

  const RouterNamedStats named_;

private:
  void incClusterScopeCounter(const Upstream::ClusterInfo& cluster,
                              Upstream::HostDescriptionConstSharedPtr upstream_host,
                              const Stats::StatName& stat_name) const {
    const Stats::SymbolTable::StoragePtr stat_name_storage = symbol_table_.join({stat_name});
    cluster.statsScope().counterFromStatName(Stats::StatName(stat_name_storage.get())).inc();
    const Stats::SymbolTable::StoragePtr zone_stat_name_storage =
        upstreamZoneStatName(upstream_host, stat_name);
    if (zone_stat_name_storage) {
      cluster.statsScope().counterFromStatName(Stats::StatName(zone_stat_name_storage.get())).inc();
    }
  }

  void recordClusterScopeHistogram(const Upstream::ClusterInfo& cluster,
                                   Upstream::HostDescriptionConstSharedPtr upstream_host,
                                   const Stats::StatName& stat_name, Stats::Histogram::Unit unit,
                                   uint64_t value) const {
    const Stats::SymbolTable::StoragePtr stat_name_storage = symbol_table_.join({stat_name});
    cluster.statsScope()
        .histogramFromStatName(Stats::StatName(stat_name_storage.get()), unit)
        .recordValue(value);
    const Stats::SymbolTable::StoragePtr zone_stat_name_storage =
        upstreamZoneStatName(upstream_host, stat_name);
    if (zone_stat_name_storage) {
      cluster.statsScope()
          .histogramFromStatName(Stats::StatName(zone_stat_name_storage.get()), unit)
          .recordValue(value);
    }
  }

  Stats::SymbolTable::StoragePtr
  upstreamZoneStatName(Upstream::HostDescriptionConstSharedPtr upstream_host,
                       const Stats::StatName& stat_name) const {
    if (!upstream_host || local_zone_name_.empty()) {
      return nullptr;
    }
    const auto& upstream_zone_name = upstream_host->localityZoneStatName();
    if (upstream_zone_name.empty()) {
      return nullptr;
    }
    return symbol_table_.join({zone_, local_zone_name_, upstream_zone_name, stat_name});
  }

  Stats::StatNameSetPtr stat_name_set_;
  Stats::SymbolTable& symbol_table_;
  const Stats::StatName upstream_rq_call_;
  const Stats::StatName upstream_rq_oneway_;
  const Stats::StatName upstream_rq_invalid_type_;
  const Stats::StatName upstream_resp_reply_;
  const Stats::StatName upstream_resp_reply_success_;
  const Stats::StatName upstream_resp_reply_error_;
  const Stats::StatName upstream_resp_exception_;
  const Stats::StatName upstream_resp_exception_local_;
  const Stats::StatName upstream_resp_exception_remote_;
  const Stats::StatName upstream_resp_invalid_type_;
  const Stats::StatName upstream_resp_decoding_error_;
  const Stats::StatName upstream_rq_time_;
  const Stats::StatName upstream_rq_size_;
  const Stats::StatName upstream_resp_size_;
  const Stats::StatName zone_;
  const Stats::StatName local_zone_name_;
  const Stats::StatName upstream_cx_drain_close_;
  const Stats::StatName downstream_cx_partial_response_close_;
  const Stats::StatName downstream_cx_underflow_response_close_;
};

/**
 * This interface is used by an upstream request to communicate its state.
 */
class RequestOwner : public ProtocolConverter, public Logger::Loggable<Logger::Id::thrift> {
public:
  RequestOwner(Upstream::ClusterManager& cluster_manager, const RouterStats& stats)
      : cluster_manager_(cluster_manager), stats_(stats) {}
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
   * @return Upstream::ClusterManager& the cluster manager.
   */
  Upstream::ClusterManager& clusterManager() { return cluster_manager_; }

  /**
   * @return Upstream::Cluster& the upstream cluster associated with the request.
   */
  const Upstream::ClusterInfo& cluster() const { return *cluster_; }

  /**
   * @return RouterStats the common router stats.
   */
  const RouterStats& stats() { return stats_; }

  virtual void onReset() {}

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
      stats().named_.unknown_cluster_.inc();
      return {AppException(AppExceptionType::InternalError,
                           fmt::format("unknown cluster '{}'", cluster_name)),
              absl::nullopt};
    }

    cluster_ = cluster->info();
    ENVOY_LOG(debug, "cluster '{}' match for method '{}'", cluster_name, metadata->methodName());

    switch (metadata->messageType()) {
    case MessageType::Call:
      stats().incRequestCall(*cluster_);
      break;

    case MessageType::Oneway:
      stats().incRequestOneWay(*cluster_);
      break;

    default:
      stats().incRequestInvalid(*cluster_);
      break;
    }

    if (cluster_->maintenanceMode()) {
      stats().named_.upstream_rq_maintenance_mode_.inc();
      if (metadata->messageType() == MessageType::Call) {
        stats().incResponseLocalException(*cluster_);
      }
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
      stats().named_.no_healthy_upstream_.inc();
      if (metadata->messageType() == MessageType::Call) {
        stats().incResponseLocalException(*cluster_);
      }
      return {AppException(AppExceptionType::InternalError,
                           fmt::format("no healthy upstream for '{}'", cluster_name)),
              absl::nullopt};
    }

    const auto passthrough_supported =
        (transport == TransportType::Framed || transport == TransportType::Header) &&
        (final_transport == TransportType::Framed || final_transport == TransportType::Header) &&
        protocol == final_protocol && final_protocol != ProtocolType::Twitter;
    UpstreamRequestInfo result = {passthrough_supported, final_transport, final_protocol,
                                  conn_pool_data};
    return {absl::nullopt, result};
  }

  Upstream::ClusterInfoConstSharedPtr cluster_;

private:
  Upstream::ClusterManager& cluster_manager_;
  const RouterStats& stats_;
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
