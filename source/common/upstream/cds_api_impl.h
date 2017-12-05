#pragma once

#include <functional>

#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/local_info/local_info.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"

#include "api/cds.pb.h"

namespace Envoy {
namespace Upstream {

/**
 * CDS API implementation that fetches via Subscription.
 */
class CdsApiImpl : public CdsApi,
                   Config::SubscriptionCallbacks<envoy::api::v2::Cluster>,
                   Logger::Loggable<Logger::Id::upstream> {
public:
  static CdsApiPtr create(const envoy::api::v2::ConfigSource& cds_config,
                          const Optional<envoy::api::v2::ConfigSource>& eds_config,
                          ClusterManager& cm, Event::Dispatcher& dispatcher,
                          Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                          Stats::Scope& scope);

  // Upstream::CdsApi
  void initialize() override { subscription_->start({}, *this); }
  void setInitializedCb(std::function<void()> callback) override {
    initialize_callback_ = callback;
  }
  const std::string versionInfo() const override { return subscription_->versionInfo(); }

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const ResourceVector& resources) override;
  void onConfigUpdateFailed(const EnvoyException* e) override;

private:
  CdsApiImpl(const envoy::api::v2::ConfigSource& cds_config,
             const Optional<envoy::api::v2::ConfigSource>& eds_config, ClusterManager& cm,
             Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
             const LocalInfo::LocalInfo& local_info, Stats::Scope& scope);
  void runInitializeCallbackIfAny();

  ClusterManager& cm_;
  std::unique_ptr<Config::Subscription<envoy::api::v2::Cluster>> subscription_;
  std::function<void()> initialize_callback_;
  Stats::ScopePtr scope_;
};

} // namespace Upstream
} // namespace Envoy
