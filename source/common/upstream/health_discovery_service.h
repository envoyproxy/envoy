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
#include "common/upstream/locality_endpoint.h"
#include "common/upstream/upstream_impl.h"

#include "server/transport_socket_config_impl.h"

#include "extensions/transport_sockets/well_known_names.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Upstream {

using HostsMap = absl::flat_hash_map<LocalityEndpointTuple, HostSharedPtr, LocalityEndpointHash,
                                     LocalityEndpointEqualTo>;
using HealthCheckerMap =
    absl::flat_hash_map<envoy::config::core::v3::HealthCheck, Upstream::HealthCheckerSharedPtr,
                        HealthCheckerHash, HealthCheckerEqualTo>;

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
             Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
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
  // Compare changes in the cluster proto, and update parts of the cluster as needed.
  void update(Server::Admin& admin, envoy::config::cluster::v3::Cluster cluster,
              ClusterInfoFactory& info_factory, ClusterManager& cm,
              const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
              Singleton::Manager& singleton_manager, ThreadLocal::SlotAllocator& tls,
              ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api,
              AccessLog::AccessLogManager& access_log_manager, Runtime::Loader& runtime);
  // Creates healthcheckers and adds them to the list, then does initial start.
  void initHealthchecks(AccessLog::AccessLogManager& access_log_manager, Runtime::Loader& runtime,
                        Event::Dispatcher& dispatcher, Api::Api& api);

  std::vector<Upstream::HealthCheckerSharedPtr> healthCheckers() { return health_checkers_; };
  std::vector<HostSharedPtr> hosts() { return *hosts_; };

protected:
  PrioritySetImpl priority_set_;
  HealthCheckerSharedPtr health_checker_;
  Outlier::DetectorSharedPtr outlier_detector_;

private:
  std::function<void()> initialization_complete_callback_;

  Runtime::Loader& runtime_;
  envoy::config::cluster::v3::Cluster cluster_;
  const envoy::config::core::v3::BindConfig& bind_config_;
  Stats::Store& stats_;
  Ssl::ContextManager& ssl_context_manager_;
  bool added_via_api_;
  bool initialized_ = false;
  uint64_t config_hash_;
  uint64_t socket_match_hash_;

  HostVectorSharedPtr hosts_;
  HostsPerLocalitySharedPtr hosts_per_locality_;
  HostsMap hosts_map_;
  ClusterInfoConstSharedPtr info_;
  std::vector<Upstream::HealthCheckerSharedPtr> health_checkers_;
  HealthCheckerMap health_checkers_map_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;

  void updateHealthchecks(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck>& health_checks,
      AccessLog::AccessLogManager& access_log_manager, Runtime::Loader& runtime,
      Event::Dispatcher& dispatcher, Api::Api& api);
  void
  updateHosts(const Protobuf::RepeatedPtrField<envoy::config::endpoint::v3::LocalityLbEndpoints>&
                  locality_endpoints,
              bool update_socket_matches);
};

using HdsClusterPtr = std::shared_ptr<HdsCluster>;

/**
 * All hds stats. @see stats_macros.h
 */
#define ALL_HDS_STATS(COUNTER)                                                                     \
  COUNTER(requests)                                                                                \
  COUNTER(responses)                                                                               \
  COUNTER(errors)                                                                                  \
  COUNTER(updates)

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
              Ssl::ContextManager& ssl_context_manager, ClusterInfoFactory& info_factory,
              AccessLog::AccessLogManager& access_log_manager, ClusterManager& cm,
              const LocalInfo::LocalInfo& local_info, Server::Admin& admin,
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
  envoy::config::cluster::v3::Cluster
  createClusterConfig(const envoy::service::health::v3::ClusterHealthCheck& cluster_health_check);
  void updateHdsCluster(HdsClusterPtr cluster,
                        const envoy::config::cluster::v3::Cluster& cluster_health_check);
  HdsClusterPtr createHdsCluster(const envoy::config::cluster::v3::Cluster& cluster_health_check);
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
  ClusterInfoFactory& info_factory_;
  AccessLog::AccessLogManager& access_log_manager_;
  ClusterManager& cm_;
  const LocalInfo::LocalInfo& local_info_;
  Server::Admin& admin_;
  Singleton::Manager& singleton_manager_;
  ThreadLocal::SlotAllocator& tls_;

  envoy::service::health::v3::HealthCheckRequestOrEndpointHealthResponse health_check_request_;
  uint64_t specifier_hash_;

  std::vector<std::string> clusters_;
  std::vector<HdsClusterPtr> hds_clusters_;
  absl::flat_hash_map<std::string, HdsClusterPtr> hds_clusters_name_map_;

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
