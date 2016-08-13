#pragma once

#include "upstream_impl.h"

#include "envoy/http/async_client.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"

namespace Upstream {

/**
 * Global configuration for any SDS clusters.
 */
struct SdsConfig {
  std::string local_zone_name_;
  std::string sds_cluster_name_;
  std::chrono::milliseconds refresh_delay_;
};

/**
 * Cluster implementation that reads host information from the service discovery service.
 */
class SdsClusterImpl : public BaseDynamicClusterImpl, public Http::AsyncClient::Callbacks {
public:
  SdsClusterImpl(const Json::Object& config, Stats::Store& stats,
                 Ssl::ContextManager& ssl_context_manager, const SdsConfig& sds_config,
                 ClusterManager& cm, Event::Dispatcher& dispatcher,
                 Runtime::RandomGenerator& random);

  ~SdsClusterImpl();

  /**
   * SDS clusters do not begin host refresh in the constructor because SDS typically depends on
   * another upstream cluster that must initialize first. This allows the cluster manager to
   * initialize the SDS clusters when the other clusters have been initialized. The health checker
   * will also be installed by this time.
   */
  void initialize() { refreshHosts(); }

  // Upstream::Cluster
  void shutdown() override;

private:
  void parseSdsResponse(Http::Message& response);
  void refreshHosts();
  void requestComplete();

  // Http::AsyncClient::Callbacks
  void onSuccess(Http::MessagePtr&& response) override;
  void onFailure(Http::AsyncClient::FailureReason reason) override;

  ClusterManager& cm_;
  const SdsConfig& sds_config_;
  const std::string service_name_;
  Runtime::RandomGenerator& random_;
  Event::TimerPtr refresh_timer_;
  Http::AsyncClient::Request* active_request_{};
  uint64_t pending_health_checks_{};
};

} // Upstream
