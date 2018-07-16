#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/service/discovery/v2/hds.pb.h"
#include "envoy/ssl/context_manager.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/upstream.h"

#include "common/common/logger.h"
#include "common/grpc/async_client_impl.h"
#include "common/network/resolver_impl.h"
#include "common/upstream/health_checker_impl.h"
#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {
/**
 * Implementation of Upstream::Cluster for hds clusters (clusters that are used
 * by HdsDelegates
 */

class HdsCluster : public Cluster, Logger::Loggable<Logger::Id::upstream> {
public:
  void setHealthChecker(const HealthCheckerSharedPtr& health_checker);

  static ClusterSharedPtr create();

  InitializePhase initializePhase() const override { return InitializePhase::Primary; }
  PrioritySet& prioritySet() override { return priority_set_; }
  const PrioritySet& prioritySet() const override { return priority_set_; }

  void setOutlierDetector(const Outlier::DetectorSharedPtr& outlier_detector);

  HealthChecker* healthChecker() override { return health_checker_.get(); }
  ClusterInfoConstSharedPtr info() const override { return info_; }
  Outlier::Detector* outlierDetector() override { return outlier_detector_.get(); }
  const Outlier::Detector* outlierDetector() const override { return outlier_detector_.get(); }
  void initialize(std::function<void()> callback) override;
  HdsCluster(Runtime::Loader& runtime, const envoy::api::v2::Cluster& cluster,
             const envoy::api::v2::core::BindConfig& bind_config, Stats::Store& stats,
             Ssl::ContextManager& ssl_context_manager, Secret::SecretManager& secret_manager,
             bool added_via_api);
  const Network::Address::InstanceConstSharedPtr
  resolveProtoAddress2(const envoy::api::v2::core::Address& address);

protected:
  ClusterInfoConstSharedPtr info_;
  PrioritySetImpl priority_set_;
  HealthCheckerSharedPtr health_checker_;
  Outlier::DetectorSharedPtr outlier_detector_;
  Runtime::Loader& runtime_;
  static HostVectorConstSharedPtr createHealthyHostList(const HostVector& hosts);
  static HostsPerLocalityConstSharedPtr createHealthyHostLists(const HostsPerLocality& hosts);

private:
  std::function<void()> initialization_complete_callback_;
  HostVectorSharedPtr initial_hosts_;
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

class HdsDelegate
    : Grpc::TypedAsyncStreamCallbacks<envoy::service::discovery::v2::HealthCheckSpecifier>,
      Logger::Loggable<Logger::Id::upstream> {
public:
  HdsDelegate(const envoy::api::v2::core::Node& node, Stats::Scope& scope,
              Grpc::AsyncClientPtr async_client, Event::Dispatcher& dispatcher,
              Runtime::Loader& runtime, Envoy::Stats::Store& stats,
              Ssl::ContextManager& ssl_context_manager, Secret::SecretManager& secret_manager,
              Runtime::RandomGenerator& random);

  // Grpc::TypedAsyncStreamCallbacks
  void onCreateInitialMetadata(Http::HeaderMap& metadata) override;
  void onReceiveInitialMetadata(Http::HeaderMapPtr&& metadata) override;
  void onReceiveMessage(
      std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier>&& message) override;
  void onReceiveTrailingMetadata(Http::HeaderMapPtr&& metadata) override;
  void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override;
  void sendResponse();
  // TODO(htuch): Make this configurable or some static.
  const uint32_t RETRY_DELAY_MS = 5000;
  uint32_t SERVER_RESPONSE_S = 1;

private:
  void setRetryTimer();
  void setServerResponseTimer();
  void establishNewStream();
  void sendHealthCheckRequest();
  void handleFailure();
  void
  processMessage(std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier>&& message);

  HdsDelegateStats stats_;
  Grpc::AsyncClientPtr async_client_;
  Grpc::AsyncStream* stream_{};
  const Protobuf::MethodDescriptor& service_method_;
  Event::TimerPtr retry_timer_;
  Event::TimerPtr response_timer_;
  envoy::service::discovery::v2::HealthCheckRequest health_check_request_;
  std::unique_ptr<envoy::service::discovery::v2::HealthCheckSpecifier> health_check_message_;
  std::vector<std::string> clusters_;
  Runtime::Loader& runtime_;
  Envoy::Stats::Store& store_stats;
  Ssl::ContextManager& ssl_context_manager_;
  Secret::SecretManager& secret_manager_;
  Runtime::RandomGenerator& random_;
  Event::Dispatcher& dispatcher_;

  Event::TimerPtr server_responce_timer_;

  std::vector<std::vector<Upstream::HealthCheckerSharedPtr>> health_checkers_ptr;
  std::vector<envoy::api::v2::Cluster> clusters_config_;
  HdsClusterPtr cluster_;
  std::vector<HdsClusterPtr> hds_clusters_;
};

typedef std::unique_ptr<HdsDelegate> HdsDelegatePtr;

} // namespace Upstream
} // namespace Envoy
