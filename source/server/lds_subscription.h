#pragma once

#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/config/subscription.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/http/rest_api_fetcher.h"
#include "common/json/json_validator.h"

namespace Envoy {
namespace Server {

/**
 * Subscription implementation that reads listener information from the v1 REST Listener Discovery
 * Service.
 */
class LdsSubscription : public Http::RestApiFetcher,
                        public Config::Subscription<envoy::api::v2::Listener>,
                        Logger::Loggable<Logger::Id::upstream> {
public:
  LdsSubscription(Config::SubscriptionStats stats,
                  const envoy::api::v2::core::ConfigSource& lds_config,
                  Upstream::ClusterManager& cm, Event::Dispatcher& dispatcher,
                  Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                  const Stats::StatsOptions& stats_options);

private:
  // Config::Subscription
  void start(const std::vector<std::string>& resources,
             Config::SubscriptionCallbacks<envoy::api::v2::Listener>& callbacks) override {
    // LDS subscribes to all clusters.
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
  Config::SubscriptionCallbacks<envoy::api::v2::Listener>* callbacks_ = nullptr;
  Config::SubscriptionStats stats_;
  const Stats::StatsOptions& stats_options_;
};

} // namespace Server
} // namespace Envoy
