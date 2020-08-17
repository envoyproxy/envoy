#pragma once

#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/server/transport_socket_config.h"
#include "envoy/service/health/v3/hds.pb.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/upstream.h"

#include "common/common/backoff_strategy.h"
#include "common/common/logger.h"
#include "common/common/macros.h"
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
  ClusterInfoConstSharedPtr createClusterInfo(const CreateClusterInfoParams& params) override;
};

// TODO(lilika): Add HdsClusters to the /clusters endpoint to get detailed stats about each HC host.

/**
 * Implementation of Upstream::Cluster for hds clusters, clusters that are used
 * by HdsDelegates
 */
class HdsCluster : public Cluster, Logger::Loggable<Logger::Id::upstream> {
public:
  static ClusterSharedPtr create();
  HdsCluster(Server::Admin& admin, Runtime::Loader& runtime,
             envoy::config::cluster::v3::Cluster cluster,
             const envoy::config::core::v3::BindConfig& bind_config, Stats::Store& stats,
             Ssl::ContextManager& ssl_context_manager, bool added_via_api,
             ClusterInfoFactory& info_factory, ClusterManager& cm,
             const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
             Random::RandomGenerator& random, Singleton::Manager& singleton_manager,
             ThreadLocal::SlotAllocator& tls,
             ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api);

  // Upstream::Cluster
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
                         Random::RandomGenerator& random, Event::Dispatcher& dispatcher,
                         Api::Api& api);

  std::vector<Upstream::HealthCheckerSharedPtr> healthCheckers() { return health_checkers_; };

protected:
  PrioritySetImpl priority_set_;
  HealthCheckerSharedPtr health_checker_;
  Outlier::DetectorSharedPtr outlier_detector_;

private:
  std::function<void()> initialization_complete_callback_;

  Runtime::Loader& runtime_;
  const envoy::config::cluster::v3::Cluster cluster_;
  const envoy::config::core::v3::BindConfig& bind_config_;
  Stats::Store& stats_;
  Ssl::ContextManager& ssl_context_manager_;
  bool added_via_api_;

  HostVectorSharedPtr initial_hosts_;
  ClusterInfoConstSharedPtr info_;
  std::vector<Upstream::HealthCheckerSharedPtr> health_checkers_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
};

using HdsClusterPtr = std::shared_ptr<HdsCluster>;

/**
 * All hds stats. @see stats_macros.h
 */
#define ALL_HDS_STATS(COUNTER)                                                                     \
  COUNTER(requests)                                                                                \
  COUNTER(responses)                                                                               \
  COUNTER(errors)

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
class HdsDelegate : Grpc::AsyncStreamCallbacks<envoy::service::health::v3::HealthCheckSpecifier>,
                    Logger::Loggable<Logger::Id::upstream> {
public:
  HdsDelegate(Stats::Scope& scope, Grpc::RawAsyncClientPtr async_client,
              envoy::config::core::v3::ApiVersion transport_api_version,
              Event::Dispatcher& dispatcher, Runtime::Loader& runtime, Envoy::Stats::Store& stats,
              Ssl::ContextManager& ssl_context_manager, Random::RandomGenerator& random,
              ClusterInfoFactory& info_factory, AccessLog::AccessLogManager& access_log_manager,
              ClusterManager& cm, const LocalInfo::LocalInfo& local_info, Server::Admin& admin,
              Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
              ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api);

  // Grpc::AsyncStreamCallbacks
  void onCreateInitialMetadata(Http::RequestHeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&& metadata) override;
  void onReceiveMessage(
      std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier>&& message) override;
  void onReceiveTrailingMetadata(Http::ResponseTrailerMapPtr&& metadata) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;
  envoy::service::health::v3::HealthCheckRequestOrEndpointHealthResponse sendResponse();

  std::vector<HdsClusterPtr> hdsClusters() { return hds_clusters_; };

private:
  friend class HdsDelegateFriend;

  void setHdsRetryTimer();
  void setHdsStreamResponseTimer();
  void handleFailure();
  // Establishes a connection with the management server
  void establishNewStream();
  void processMessage(std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier>&& message);

  HdsDelegateStats stats_;
  const Protobuf::MethodDescriptor& service_method_;

  Grpc::AsyncClient<envoy::service::health::v3::HealthCheckRequestOrEndpointHealthResponse,
                    envoy::service::health::v3::HealthCheckSpecifier>
      async_client_;
  const envoy::config::core::v3::ApiVersion transport_api_version_;
  Grpc::AsyncStream<envoy::service::health::v3::HealthCheckRequestOrEndpointHealthResponse>
      stream_{};
  Event::Dispatcher& dispatcher_;
  Runtime::Loader& runtime_;
  Envoy::Stats::Store& store_stats_;
  Ssl::ContextManager& ssl_context_manager_;
  Random::RandomGenerator& random_;
  ClusterInfoFactory& info_factory_;
  AccessLog::AccessLogManager& access_log_manager_;
  ClusterManager& cm_;
  const LocalInfo::LocalInfo& local_info_;
  Server::Admin& admin_;
  Singleton::Manager& singleton_manager_;
  ThreadLocal::SlotAllocator& tls_;

  envoy::service::health::v3::HealthCheckRequestOrEndpointHealthResponse health_check_request_;
  std::unique_ptr<envoy::service::health::v3::HealthCheckSpecifier> health_check_message_;

  std::vector<std::string> clusters_;
  std::vector<HdsClusterPtr> hds_clusters_;

  Event::TimerPtr hds_stream_response_timer_;
  Event::TimerPtr hds_retry_timer_;
  BackOffStrategyPtr backoff_strategy_;

  // Soft limit on size of the cluster’s connections read and write buffers.
  static constexpr uint32_t ClusterConnectionBufferLimitBytes = 32768;

  // TODO(lilika): Add API knob for ClusterTimeoutSeconds, instead of
  // hardcoding it.
  // The timeout for new network connections to hosts in the cluster.
  static constexpr uint32_t ClusterTimeoutSeconds = 1;

  // How often envoy reports the healthcheck results to the server
  uint32_t server_response_ms_ = 0;

  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Api::Api& api_;
};

using HdsDelegatePtr = std::unique_ptr<HdsDelegate>;

} // namespace Upstream
} // namespace Envoy
