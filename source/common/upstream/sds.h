#pragma once

#include "upstream_impl.h"

#include "envoy/http/async_client.h"
#include "envoy/runtime/runtime.h"
#include "envoy/upstream/cluster_manager.h"

namespace Upstream {

/**
 * Cluster implementation that reads host information from the service discovery service.
 */
class SdsClusterImpl : public BaseDynamicClusterImpl, public Http::AsyncClient::Callbacks {
public:
  SdsClusterImpl(const Json::Object& config, Runtime::Loader& runtime, Stats::Store& stats,
                 Ssl::ContextManager& ssl_context_manager, const SdsConfig& sds_config,
                 ClusterManager& cm, Event::Dispatcher& dispatcher,
                 Runtime::RandomGenerator& random);

  ~SdsClusterImpl();

  // Upstream::Cluster
  void initialize() override { refreshHosts(); }
  InitializePhase initializePhase() const override { return InitializePhase::Secondary; }

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
