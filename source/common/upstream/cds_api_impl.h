#pragma once

#include <functional>

#include "envoy/api/api.h"
#include "envoy/api/v2/cds.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/local_info/local_info.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/logger.h"
#include "common/upstream/cds_api_incremental_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * CDS API implementation that fetches via Subscription.
 */
class CdsApiImpl : public CdsApiIncrementalImpl,
                   Config::SubscriptionCallbacks<envoy::api::v2::Cluster> {
public:
  static CdsApiPtr create(const envoy::api::v2::core::ConfigSource& cds_config, ClusterManager& cm,
                          Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                          const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
                          Api::Api& api);

  // Config::SubscriptionCallbacks
<<<<<<< HEAD
  // TODO(fredlas) deduplicate
  void onConfigUpdate(const ResourceVector& resources, const std::string& version_info) override;
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>&,
                      const Protobuf::RepeatedPtrField<std::string>&, const std::string&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
=======
  // TODO(fredlas) use ResourceVector typedef once all xDS have incremental implemented,
  //               so that we can rely on IncrementalSubscriptionCallbacks to provide the typedef.
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Cluster>& resources,
                      const std::string& version_info) override;
>>>>>>> snapshot
  void onConfigUpdateFailed(const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::api::v2::Cluster>(resource).name();
  }

private:
  CdsApiImpl(const envoy::api::v2::core::ConfigSource& cds_config, ClusterManager& cm,
             Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
<<<<<<< HEAD
             const LocalInfo::LocalInfo& local_info, Stats::Scope& scope);
=======
             const LocalInfo::LocalInfo& local_info, Stats::Scope& scope, Api::Api& api);
  void runInitializeCallbackIfAny();
>>>>>>> filesystem: convert free functions to object methods (#5692)

  std::unique_ptr<Config::Subscription<envoy::api::v2::Cluster>> subscription_;
  std::string version_info_;
  std::function<void()> initialize_callback_;
};

} // namespace Upstream
} // namespace Envoy
