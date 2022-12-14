#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/certificate_provider/certificate_provider.h"
#include "envoy/common/random_generator.h"
#include "envoy/event/timer.h"
#include "envoy/extensions/filters/network/bumping/v3/bumping.pb.h"
#include "envoy/http/header_evaluator.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/timespan.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/tcp/upstream.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/network/cidr_range.h"
#include "source/common/network/filter_impl.h"
#include "source/common/network/utility.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Bumping {

/**
 * All bumping stats. @see stats_macros.h
 */
#define ALL_BUMPING_STATS(COUNTER) COUNTER(downstream_cx_no_route)
/**
 * Struct definition for all bumping stats. @see stats_macros.h
 */
struct BumpingStats {
  ALL_BUMPING_STATS(GENERATE_COUNTER_STRUCT);
};

/**
 * Route is an individual resolved route for a connection.
 */
class Route {
public:
  virtual ~Route() = default;

  /**
   * Check whether this route matches a given connection.
   * @param connection supplies the connection to test against.
   * @return bool true if this route matches a given connection.
   */
  virtual bool matches(Network::Connection& connection) const PURE;

  /**
   * @return const std::string& the upstream cluster that owns the route.
   */
  virtual const std::string& clusterName() const PURE;
};

using RouteConstSharedPtr = std::shared_ptr<const Route>;

/**
 * Filter configuration.
 *
 * This configuration holds a TLS slot, and therefore it must be destructed
 * on the main thread.
 */
class Config {
public:
  Config(const envoy::extensions::filters::network::bumping::v3::Bumping& config,
         Server::Configuration::FactoryContext& context);

  /**
   * Find out which cluster an upstream connection should be opened to based on the
   * parameters of a downstream connection.
   * @param connection supplies the parameters of the downstream connection for
   * which the proxy needs to open the corresponding upstream.
   * @return the route to be used for the upstream connection.
   * If no route applies, returns nullptr.
   */
  RouteConstSharedPtr getRoute();
  const BumpingStats& stats() { return stats_; }
  const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() { return access_logs_; }
  uint32_t maxConnectAttempts() const { return max_connect_attempts_; }
  const absl::optional<std::chrono::milliseconds>& idleTimeout() { return idle_timeout_; }
  Event::Dispatcher& main_dispatcher_;
  Server::Configuration::FactoryContext& context_;
  CertificateProvider::CertificateProviderSharedPtr tls_certificate_provider_;
  std::string tls_certificate_name_;

private:
  struct SimpleRouteImpl : public Route {
    SimpleRouteImpl(const Config& parent, absl::string_view cluster_name);

    // Route
    bool matches(Network::Connection&) const override { return true; }
    const std::string& clusterName() const override { return cluster_name_; }

    const Config& parent_;
    std::string cluster_name_;
  };
  RouteConstSharedPtr default_route_;

  static BumpingStats generateStats(Stats::Scope& scope);

  std::vector<AccessLog::InstanceSharedPtr> access_logs_;
  absl::optional<std::chrono::milliseconds> idle_timeout_;
  const Stats::ScopeSharedPtr stats_scope_;
  const BumpingStats stats_;
  const uint32_t max_connect_attempts_;
  absl::optional<std::chrono::milliseconds> max_downstream_connection_duration_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

class BumpingMetadata : public CertificateProvider::OnDemandUpdateMetadata {
public:
  BumpingMetadata(Envoy::Ssl::ConnectionInfoConstSharedPtr info) : info_(info){};

  Envoy::Ssl::ConnectionInfoConstSharedPtr connectionInfo() const override { return info_; }

private:
  Envoy::Ssl::ConnectionInfoConstSharedPtr info_;
};

/**
 * An implementation of a Bumping filter. This filter will instantiate a new outgoing TCP
 * connection using TCP connection pool for the configured cluster. The established connection
 * is only used for getting upstream cert, no data exchange happens.
 */
class Filter : public Network::ReadFilter,
               public Upstream::LoadBalancerContextBase,
               protected Logger::Loggable<Logger::Id::filter>,
               public TcpProxy::GenericConnectionPoolCallbacks,
               public CertificateProvider::OnDemandUpdateCallbacks {
public:
  Filter(ConfigSharedPtr config, Upstream::ClusterManager& cluster_manager);
  ~Filter() override;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  // GenericConnectionPoolCallbacks
  void onGenericPoolReady(StreamInfo::StreamInfo* info,
                          std::unique_ptr<TcpProxy::GenericUpstream>&& upstream,
                          Upstream::HostDescriptionConstSharedPtr& host,
                          const Network::ConnectionInfoProvider& address_provider,
                          Ssl::ConnectionInfoConstSharedPtr ssl_info) override;
  void onGenericPoolFailure(ConnectionPool::PoolFailureReason reason,
                            absl::string_view failure_reason,
                            Upstream::HostDescriptionConstSharedPtr host) override;

  // Upstream::LoadBalancerContext
  const Router::MetadataMatchCriteria* metadataMatchCriteria() override { return nullptr; }
  absl::optional<uint64_t> computeHashKey() override { return {}; }
  const Network::Connection* downstreamConnection() const override {
    return &read_callbacks_->connection();
  }

  Network::TransportSocketOptionsConstSharedPtr upstreamTransportSocketOptions() const override {
    return transport_socket_options_;
  }

  Network::Socket::OptionsSharedPtr upstreamSocketOptions() const override {
    return upstream_options_;
  }

  // CertificateProvider::OnDemandUpdateCallbacks
  void onCacheHit(const std::string host, bool check_only) override;
  void onCacheMiss(const std::string host, bool check_only) override;

  void requestCertificate(Ssl::ConnectionInfoConstSharedPtr info);

  struct UpstreamCallbacks : public Tcp::ConnectionPool::UpstreamCallbacks {
    UpstreamCallbacks(Filter* parent) : parent_(parent) {}

    // Tcp::ConnectionPool::UpstreamCallbacks
    void onUpstreamData(Buffer::Instance&, bool) override{};
    void onEvent(Network::ConnectionEvent event) override;
    void onAboveWriteBufferHighWatermark() override{};
    void onBelowWriteBufferLowWatermark() override{};

    Filter* parent_{};
  };

  StreamInfo::StreamInfo& getStreamInfo();

protected:
  enum class UpstreamFailureReason {
    ConnectFailed,
    NoHealthyUpstream,
    ResourceLimitExceeded,
    NoRoute,
  };

  virtual RouteConstSharedPtr pickRoute() { return config_->getRoute(); }

  virtual void onInitFailure(UpstreamFailureReason) {
    read_callbacks_->connection().close(Network::ConnectionCloseType::NoFlush);
  }

  // Create connection to the upstream cluster. This function can be repeatedly called on upstream
  // connection failure.
  Network::FilterStatus establishUpstreamConnection();

  // The callback upon on demand cluster discovery response.
  // void onClusterDiscoveryCompletion(Upstream::ClusterDiscoveryStatus cluster_status);

  bool maybeTunnel(Upstream::ThreadLocalCluster& cluster);
  void onConnectTimeout();
  void onUpstreamEvent(Network::ConnectionEvent event);
  void onUpstreamConnection();

  const ConfigSharedPtr config_;
  Upstream::ClusterManager& cluster_manager_;
  Network::ReadFilterCallbacks* read_callbacks_{};

  std::shared_ptr<UpstreamCallbacks> upstream_callbacks_; // shared_ptr required for passing as a
                                                          // read filter.
  // The upstream handle (either TCP or HTTP). This is set in onGenericPoolReady and should persist
  // until either the upstream or downstream connection is terminated.
  std::unique_ptr<TcpProxy::GenericUpstream> upstream_;
  // The connection pool used to set up |upstream_|.
  // This will be non-null from when an upstream connection is attempted until
  // it either succeeds or fails.
  std::unique_ptr<TcpProxy::GenericConnPool> generic_conn_pool_;
  RouteConstSharedPtr route_;
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options_;
  Network::Socket::OptionsSharedPtr upstream_options_;
  uint32_t connect_attempts_{};
  bool connecting_{};
  Envoy::CertificateProvider::OnDemandUpdateHandlePtr on_demand_handle_;
};
} // namespace Bumping
} // namespace Envoy
