#pragma once

#include <functional>

#include "envoy/api/v2/cds.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/local_info/local_info.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/http/rest_api_fetcher.h"

namespace Envoy {
namespace Upstream {

/**
 * Subscription implementation that reads cluster information from the v1 REST Cluster Discovery
 * Service.
 */
class CdsSubscription : public Http::RestApiFetcher,
                        public Config::Subscription<envoy::api::v2::Cluster>,
                        Logger::Loggable<Logger::Id::upstream> {
public:
  CdsSubscription(Config::SubscriptionStats stats,
                  const envoy::api::v2::core::ConfigSource& cds_config,
                  const absl::optional<envoy::api::v2::core::ConfigSource>& eds_config,
                  ClusterManager& cm, Event::Dispatcher& dispatcher,
                  Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                  const Stats::StatsOptions& stats_options);

private:
  // Config::Subscription
  void start(const std::vector<std::string>& resources,
             Config::SubscriptionCallbacks<envoy::api::v2::Cluster>& callbacks) override {
    // CDS subscribes to all clusters.
    ASSERT(resources.empty());
    callbacks_ = &callbacks;
    RestApiFetcher::initialize();
  }

  void updateResources(const std::vector<std::string>& resources) override {
    // We should never hit this at runtime, since this legacy adapter is only used by CdsApiImpl
    // that doesn't do dynamic modification of resources.
    UNREFERENCED_PARAMETER(resources);
    NOT_IMPLEMENTED;
  }

  // Http::RestApiFetcher
  void createRequest(Http::Message& request) override;
  void parseResponse(const Http::Message& response) override;
  void onFetchComplete() override;
  void onFetchFailure(const EnvoyException* e) override;

  const LocalInfo::LocalInfo& local_info_;
  Config::SubscriptionCallbacks<envoy::api::v2::Cluster>* callbacks_ = nullptr;
  Config::SubscriptionStats stats_;
  const absl::optional<envoy::api::v2::core::ConfigSource>& eds_config_;
  const Stats::StatsOptions& stats_options_;
};

} // namespace Upstream
} // namespace Envoy
