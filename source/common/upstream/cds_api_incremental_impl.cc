#include "common/upstream/cds_api_incremental_impl.h"

#include <string>

#include "envoy/api/v2/cds.pb.validate.h"
#include "envoy/api/v2/cluster/outlier_detection.pb.validate.h"
#include "envoy/stats/scope.h"

#include "common/common/cleanup.h"
#include "common/config/resources.h"
#include "common/config/subscription_factory.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

CdsApiPtr CdsApiIncrementalImpl::create(const envoy::api::v2::core::ConfigSource& cds_config,
                                        ClusterManager& cm, Event::Dispatcher& dispatcher,
                                        Runtime::RandomGenerator& random,
                                        const LocalInfo::LocalInfo& local_info,
                                        Stats::Scope& scope) {
  return CdsApiPtr{
      new CdsApiIncrementalImpl(cds_config, cm, dispatcher, random, local_info, scope)};
}

CdsApiIncrementalImpl::CdsApiIncrementalImpl(const envoy::api::v2::core::ConfigSource& cds_config,
                                             ClusterManager& cm, Event::Dispatcher& dispatcher,
                                             Runtime::RandomGenerator& random,
                                             const LocalInfo::LocalInfo& local_info,
                                             Stats::Scope& scope)
    : cm_(cm), scope_(scope.createScope("cluster_manager.cds.")) {
  Config::Utility::checkLocalInfo("cds", local_info);
  subscription_ =
      Config::SubscriptionFactory::subscriptionFromConfigSource<envoy::api::v2::Cluster>(
          cds_config, local_info, dispatcher, cm, random, *scope_, "not implemented",
          "envoy.api.v2.ClusterDiscoveryService.IncrementalClusters");
}

void CdsApiIncrementalImpl::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  cm_.adsMux().pause(Config::TypeUrl::get().ClusterLoadAssignment);
  Cleanup eds_resume([this] { cm_.adsMux().resume(Config::TypeUrl::get().ClusterLoadAssignment); });

  for (const auto& resource : added_resources) {
    try {
      envoy::api::v2::Cluster cluster =
          MessageUtil::anyConvert<envoy::api::v2::Cluster>(resource.resource());
      MessageUtil::validate(cluster);
      if (cm_.addOrUpdateCluster(cluster, resource.version())) {
        ENVOY_LOG(debug, "cds: add/update cluster '{}'", cluster.name());
      }
    } catch (EnvoyException e) {
      // TODO TODO add this resource to the "resource updates that had problems" pile, to go into a
      // larger partial rejection exception
      std::cerr << "TODO TODO had a problem with a resource" << std::endl;
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

void CdsApiIncrementalImpl::onConfigUpdateFailed(const EnvoyException*) {
  // We need to allow server startup to continue, even if we have a bad config.
  runInitializeCallbackIfAny();
}

void CdsApiIncrementalImpl::runInitializeCallbackIfAny() {
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

} // namespace Upstream
} // namespace Envoy
