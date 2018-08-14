#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/service/discovery/v2/hds.pb.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/upstream.h"

#include "common/common/backoff_strategy.h"
#include "common/common/logger.h"
#include "common/config/utility.h"
#include "common/grpc/async_client_impl.h"
#include "common/network/resolver_impl.h"
#include "common/upstream/health_checker_impl.h"
#include "common/upstream/upstream_impl.h"

#include "server/transport_socket_config_impl.h"

#include "extensions/transport_sockets/well_known_names.h"

namespace Envoy {
namespace Upstream {

class ProdClusterInfoFactory : public ClusterInfoFactory, Logger::Loggable<Logger::Id::upstream> {
public:
  ClusterInfoConstSharedPtr
  createClusterInfo(Runtime::Loader& runtime, const envoy::api::v2::Cluster& cluster,
                    const envoy::api::v2::core::BindConfig& bind_config, Stats::Store& stats,
                    Ssl::ContextManager& ssl_context_manager, bool added_via_api,
                    ClusterManager& cm, const LocalInfo::LocalInfo& local_info,
                    Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random) override;
};

// TODO(lilika): Add HdsClusters to the /clusters endpoint to get detailed stats about each HC host.

/**
 * Implementation of Upstream::Cluster for hds clusters, clusters that are used
 * by HdsDelegates
 */

class HdsCluster : public Cluster, Logger::Loggable<Logger::Id::upstream> {
public:
  static ClusterSharedPtr create();
  HdsCluster(Runtime::Loader& runtime, const envoy::api::v2::Cluster& cluster,
             const envoy::api::v2::core::BindConfig& bind_config, Stats::Store& stats,
             Ssl::ContextManager& ssl_context_manager, bool added_via_api,
             ClusterInfoFactory& info_factory, ClusterManager& cm,
             const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
             Runtime::RandomGenerator& random);

  // From Upstream::Cluster
  InitializePhase initializePhase() const override { return InitializePhase::Primary; }
  PrioritySet& prioritySet() override { return priority_set_; }
  const PrioritySet& prioritySet() const override { return priority_set_; }
  void setOutlierDetector(const Outlier::DetectorSharedPtr& outlier_detector);
  HealthChecker* healthChecker() override { return health_checker_.get(); }
  ClusterInfoConstSharedPtr info() const override { return info_; }
  Outlier::Detector* outlierDetector() override { return outlier_detector_.get(); }
  const Outlier::Detector* outlierDetector() const override { return outlier_detector_.get(); }
  void initialize(std::function<void()> callback) override;

  // Creates and starts healthcheckers to its endpoints
  void startHealthchecks(AccessLog::AccessLogManager& access_log_manager, Runtime::Loader& runtime,
                         Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher);

  std::vector<Upstream::HealthCheckerSharedPtr> healthCheckers() { return health_checkers_; };

protected:
  PrioritySetImpl priority_set_;
  HealthCheckerSharedPtr health_checker_;
  Outlier::DetectorSharedPtr outlier_detector_;

  // Creates a vector containing any healthy hosts
  static HostVectorConstSharedPtr createHealthyHostList(const HostVector& hosts);

private:
  std::function<void()> initialization_complete_callback_;

  Runtime::Loader& runtime_;
  const envoy::api::v2::Cluster& cluster_;
  const envoy::api::v2::core::BindConfig& bind_config_;
  Stats::Store& stats_;
  Ssl::ContextManager& ssl_context_manager_;
  bool added_via_api_;

  HostVectorSharedPtr initial_hosts_;
  ClusterInfoConstSharedPtr info_;
  std::vector<Upstream::HealthCheckerSharedPtr> health_checkers_;
};

typedef std::shared_ptr<HdsCluster> HdsClusterPtr;

/**
 * All hds stats. @see stats_macros.h
 */
// clang-format off
#define ALL_HDS_STATS(COUNTER)                                                           \
  COUNTER(requests)                                                                                \
  COUNTER(responses)                                                                               \
  COUNTER(errors)
// clang-format on

/**
 * Struct definition for all hds stats. @see stats_macros.h
 */
struct HdsDelegateStats {
  ALL_HDS_STATS(GENERATE_COUNTER_STRUCT)
};

// TODO(lilika): Add /config_dump support for HdsDelegate

/**
 * The HdsDelegate class is responsible for receiving requests from a management
 * server with a set of hosts to healthcheck, healthchecking them, and reporting
 * back the results.
 */
class HdsDelegate
    : Grpc::TypedAsyncStreamCallbacks<envoy::service::discovery::v2::HealthCheckSpecifier>,
      Logger::Loggable<Logger::Id::upstream> {
public:
  HdsDelegate(const envoy::api::v2::core::Node& node, Stats::Scope& scope,
              Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher,
              Runtime::Loader& runtime, Envoy::Stats::Store& stats,
              Ssl::ContextManager& ssl_context_manager, Runtime::RandomGenerator& random,
              ClusterInfoFactory& info_factory, AccessLog::AccessLogManager& access_log_manager,
              ClusterManager& cm, const LocalInfo::LocalInfo& local_info);

  // Grpc::TypedAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) override;
  void onReceiveMessage(
      std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier>&& message) override;
  void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;
  envoy::service::discovery::v2::HealthCheckRequestOrEndpointHealthResponse sendResponse();

  std::vector<HdsClusterPtr> hdsClusters() { return hds_clusters_; };

private:
  friend class HdsDelegateFriend;

  void setHdsRetryTimer();
  void setHdsStreamResponseTimer();
  void handleFailure();
  // Establishes a connection with the management server
  void establishNewStream();
  void
  processMessage(std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier>&& message);

  HdsDelegateStats stats_;
  const Protobuf::MethodDescriptor& service_method_;

  Grpc::AsyncClientPtr async_client_;
  Grpc::AsyncStream* stream_{};
  Event::Dispatcher& dispatcher_;
  Runtime::Loader& runtime_;
  Envoy::Stats::Store& store_stats;
  Ssl::ContextManager& ssl_context_manager_;
  Runtime::RandomGenerator& random_;
  ClusterInfoFactory& info_factory_;
  AccessLog::AccessLogManager& access_log_manager_;
  ClusterManager& cm_;
  const LocalInfo::LocalInfo& local_info_;

  envoy::service::discovery::v2::HealthCheckRequest health_check_request_;
  std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier> health_check_message_;

  std::vector<std::string> clusters_;
  std::vector<HdsClusterPtr> hds_clusters_;

  Event::TimerPtr hds_stream_response_timer_;
  Event::TimerPtr hds_retry_timer_;
  BackOffStrategyPtr backoff_strategy_;

  /**
   * TODO(lilika): Add API knob for RetryInitialDelayMilliseconds
   * and RetryMaxDelayMilliseconds, instead of hardcoding them.
   *
   * Parameters of the jittered backoff strategy that defines how often
   * we retry to establish a stream to the management server
   */
  const uint32_t RetryInitialDelayMilliseconds = 1000;
  const uint32_t RetryMaxDelayMilliseconds = 30000;

  // Soft limit on size of the cluster’s connections read and write buffers.
  static constexpr uint32_t ClusterConnectionBufferLimitBytes = 32768;

  // TODO(lilika): Add API knob for ClusterTimeoutSeconds, instead of
  // hardcoding it.
  // The timeout for new network connections to hosts in the cluster.
  static constexpr uint32_t ClusterTimeoutSeconds = 1;

  // How often envoy reports the healthcheck results to the server
  uint32_t server_response_ms_ = 0;
};

typedef std::unique_ptr<HdsDelegate> HdsDelegatePtr;

} // namespace Upstream
} // namespace Envoy
