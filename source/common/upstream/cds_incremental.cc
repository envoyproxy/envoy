#include "common/upstream/cds_incremental.h"

#include <string>

#include "envoy/api/v2/cds.pb.validate.h"
#include "envoy/api/v2/cluster/outlier_detection.pb.validate.h"
#include "envoy/stats/scope.h"

#include "common/common/cleanup.h"
#include "common/config/incremental_subscription_factory.h"
#include "common/config/resources.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

CdsApiPtr CdsIncremental::create(const envoy::api::v2::core::ConfigSource& cds_config,
                                 const absl::optional<envoy::api::v2::core::ConfigSource>&,
                                 ClusterManager& cm, Event::Dispatcher& dispatcher,
                                 Runtime::RandomGenerator& random,
                                 const LocalInfo::LocalInfo& local_info, Stats::Scope& scope) {
  return CdsApiPtr{new CdsIncremental(cds_config, cm, dispatcher, random, local_info, scope)};
}

CdsIncremental::CdsIncremental(const envoy::api::v2::core::ConfigSource& cds_config,
                               ClusterManager& cm, Event::Dispatcher& dispatcher,
                               Runtime::RandomGenerator& random,
                               const LocalInfo::LocalInfo& local_info, Stats::Scope& scope)
    : cm_(cm), scope_(scope.createScope("cluster_manager.cds.")) {
  Config::Utility::checkLocalInfo("cds", local_info);
  subscription_ =
      Config::IncrementalSubscriptionFactory::subscriptionFromConfigSource<envoy::api::v2::Cluster>(
          cds_config, local_info, dispatcher, cm, random, *scope_,
          "envoy.api.v2.ClusterDiscoveryService.IncrementalClusters");
}

void CdsIncremental::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  cm_.adsMux().pause(Config::TypeUrl::get().ClusterLoadAssignment);
  Cleanup eds_resume([this] { cm_.adsMux().resume(Config::TypeUrl::get().ClusterLoadAssignment); });

  for (const auto& resource : added_resources) {
    envoy::api::v2::Cluster cluster =
        MessageUtil::anyConvert<envoy::api::v2::Cluster>(resource.resource());
    MessageUtil::validate(cluster);
    if (cm_.addOrUpdateCluster(cluster, resource.version())) {
      ENVOY_LOG(debug, "cds: add/update cluster '{}'", cluster.name());
    }
  }
  for (auto resource_name : removed_resources) {
    if (cm_.removeCluster(resource_name)) {
      ENVOY_LOG(debug, "cds: remove cluster '{}'", resource_name);
    }
  }

  system_version_info_ = system_version_info;
  runInitializeCallbackIfAny();
}

void CdsIncremental::onConfigUpdateFailed(const EnvoyException*) {
  // We need to allow server startup to continue, even if we have a bad config.
  runInitializeCallbackIfAny();
}

void CdsIncremental::runInitializeCallbackIfAny() {
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

} // namespace Upstream
} // namespace Envoy
