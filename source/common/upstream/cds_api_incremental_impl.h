#pragma once

#include <functional>

#include "envoy/api/v2/cds.pb.h"
#include "envoy/config/subscription.h"
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
class CdsApiIncrementalImpl : public CdsApi,
<<<<<<< HEAD:source/common/upstream/cds_api_incremental_impl.h
                              Config::SubscriptionCallbacks<envoy::api::v2::Cluster>,
                              Logger::Loggable<Logger::Id::upstream> {
=======
                              Config::IncrementalSubscriptionCallbacks<envoy::api::v2::Cluster>,
                              public Logger::Loggable<Logger::Id::upstream> {
>>>>>>> snapshot:source/common/upstream/cds_api_incremental_impl.h
public:
<<<<<<< HEAD
  static CdsApiPtr create(const envoy::api::v2::core::ConfigSource& cds_config,
                          const absl::optional<envoy::api::v2::core::ConfigSource>&,
                          ClusterManager& cm, Event::Dispatcher& dispatcher,
                          Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                          Stats::Scope& scope);
=======
  static CdsApiPtr create(const envoy::api::v2::core::ConfigSource& cds_config, ClusterManager& cm,
                          Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                          const LocalInfo::LocalInfo& local_info, Stats::Scope& scope);
>>>>>>> bring in final touches from CDS integration test PR

  // Upstream::CdsApi
  void initialize() override { subscription_->start({}, *this); }
  void setInitializedCb(std::function<void()> callback) override {
    initialize_callback_ = callback;
  }
  const std::string versionInfo() const override { return system_version_info_; }

<<<<<<< HEAD:source/common/upstream/cds_api_incremental_impl.h
  // Config::SubscriptionCallbacks
  // TODO(fredlas) deduplicate
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string& system_version_info) override;
  virtual void onConfigUpdate(const ResourceVector&, const std::string&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::Cluster>(resource).name();
  }

private:
=======
  // Config::IncrementalSubscriptionCallbacks
  void onIncrementalConfigUpdate(
      const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
      const std::string& system_version_info) override;
  void onIncrementalConfigUpdateFailed(const EnvoyException* e) override;
  std::string incrementalResourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::Cluster>(resource).name();
  }

  std::set<std::string> clusterNames();

protected:
>>>>>>> snapshot:source/common/upstream/cds_api_incremental_impl.h
  CdsApiIncrementalImpl(const envoy::api::v2::core::ConfigSource& cds_config, ClusterManager& cm,
                        Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                        const LocalInfo::LocalInfo& local_info, Stats::Scope& scope);
  void runInitializeCallbackIfAny();

private:
  ClusterManager& cm_;
  std::unique_ptr<Config::Subscription<envoy::api::v2::Cluster>> subscription_;
  std::string system_version_info_;
  std::function<void()> initialize_callback_;
  Stats::ScopePtr scope_;
};

} // namespace Upstream
} // namespace Envoy
