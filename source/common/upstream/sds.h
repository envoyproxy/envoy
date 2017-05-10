#pragma once

#include <cstdint>
#include <string>

#include "envoy/local_info/local_info.h"

#include "common/http/rest_api_fetcher.h"
#include "common/upstream/upstream_impl.h"

namespace Lyft {
namespace Upstream {

/**
 * Cluster implementation that reads host information from the service discovery service.
 */
class SdsClusterImpl : public BaseDynamicClusterImpl, Http::RestApiFetcher {
public:
  SdsClusterImpl(const Json::Object& config, Runtime::Loader& runtime, Stats::Store& stats,
                 Ssl::ContextManager& ssl_context_manager, const SdsConfig& sds_config,
                 const LocalInfo::LocalInfo& local_info, ClusterManager& cm,
                 Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random);

  // Upstream::Cluster
  void initialize() override { RestApiFetcher::initialize(); }
  InitializePhase initializePhase() const override { return InitializePhase::Secondary; }

private:
  // Http::RestApiFetcher
  void createRequest(Http::Message& request) override;
  void parseResponse(const Http::Message& response) override;
  void onFetchComplete() override;
  void onFetchFailure(EnvoyException* e) override;

  const LocalInfo::LocalInfo& local_info_;
  const std::string service_name_;
  uint64_t pending_health_checks_{};
};

} // Upstream
} // Lyft