#pragma once

#include <functional>

#include "envoy/api/v2/cds.pb.h"
#include "envoy/config/incremental_subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/local_info/local_info.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Upstream {

/**
 * Incremental CDS API implementation that fetches via IncrementalSubscription.
 */
class CdsIncremental : public CdsApi,
                       Config::IncrementalSubscriptionCallbacks<envoy::api::v2::Cluster>,
                       Logger::Loggable<Logger::Id::upstream> {
public:
  static CdsApiPtr create(const envoy::api::v2::core::ConfigSource& cds_config,
                          const absl::optional<envoy::api::v2::core::ConfigSource>&,
                          ClusterManager& cm, Event::Dispatcher& dispatcher,
                          Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                          Stats::Scope& scope);

  // Upstream::CdsApi
  void initialize() override { subscription_->start({}, *this); }
  void setInitializedCb(std::function<void()> callback) override {
    initialize_callback_ = callback;
  }
  const std::string versionInfo() const override { return version_info_; }

  // Config::IncrementalSubscriptionCallbacks
  void
  onIncrementalConfig(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& version_info) override;
  void onIncrementalConfigFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::Cluster>(resource).name();
  }

private:
  CdsIncremental(const envoy::api::v2::core::ConfigSource& cds_config, ClusterManager& cm,
                 Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                 const LocalInfo::LocalInfo& local_info, Stats::Scope& scope);
  void runInitializeCallbackIfAny();

  ClusterManager& cm_;
  std::unique_ptr<Config::IncrementalSubscription<envoy::api::v2::Cluster>> subscription_;
  std::string version_info_;
  std::function<void()> initialize_callback_;
  Stats::ScopePtr scope_;
};

} // namespace Upstream
} // namespace Envoy
