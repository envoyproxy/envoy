#include "common/upstream/cds_api_impl.h"

#include <string>

#include "envoy/api/v2/cds.pb.validate.h"
#include "envoy/api/v2/cluster/outlier_detection.pb.validate.h"

#include "common/common/cleanup.h"
#include "common/config/resources.h"
#include "common/config/subscription_factory.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"
#include "common/upstream/cds_subscription.h"

namespace Envoy {
namespace Upstream {

CdsApiPtr CdsApiImpl::create(const envoy::api::v2::core::ConfigSource& cds_config,
                             const absl::optional<envoy::api::v2::core::ConfigSource>& eds_config,
                             ClusterManager& cm, Event::Dispatcher& dispatcher,
                             Runtime::RandomGenerator& random,
                             const LocalInfo::LocalInfo& local_info, Stats::Scope& scope) {
  return CdsApiPtr{
      new CdsApiImpl(cds_config, eds_config, cm, dispatcher, random, local_info, scope)};
}

CdsApiImpl::CdsApiImpl(const envoy::api::v2::core::ConfigSource& cds_config,
                       const absl::optional<envoy::api::v2::core::ConfigSource>& eds_config,
                       ClusterManager& cm, Event::Dispatcher& dispatcher,
                       Runtime::RandomGenerator& random, const LocalInfo::LocalInfo& local_info,
                       Stats::Scope& scope)
    : cm_(cm), scope_(scope.createScope("cluster_manager.cds.")) {
  Config::Utility::checkLocalInfo("cds", local_info);

  subscription_ =
      Config::SubscriptionFactory::subscriptionFromConfigSource<envoy::api::v2::Cluster>(
          cds_config, local_info.node(), dispatcher, cm, random, *scope_,
          [this, &cds_config, &eds_config, &cm, &dispatcher, &random,
           &local_info]() -> Config::Subscription<envoy::api::v2::Cluster>* {
            return new CdsSubscription(Config::Utility::generateStats(*scope_), cds_config,
                                       eds_config, cm, dispatcher, random, local_info);
          },
          "envoy.api.v2.ClusterDiscoveryService.FetchClusters",
          "envoy.api.v2.ClusterDiscoveryService.StreamClusters");
}

void CdsApiImpl::onConfigUpdate(const ResourceVector& resources, const std::string& version_info) {
  cm_.adsMux().pause(Config::TypeUrl::get().ClusterLoadAssignment);
  Cleanup eds_resume([this] { cm_.adsMux().resume(Config::TypeUrl::get().ClusterLoadAssignment); });
  for (const auto& cluster : resources) {
    MessageUtil::validate(cluster);
  }
  // We need to keep track of which clusters we might need to remove.
  ClusterManager::ClusterInfoMap clusters_to_remove = cm_.clusters();
  for (auto& cluster : resources) {
    const std::string cluster_name = cluster.name();
    clusters_to_remove.erase(cluster_name);
    if (cm_.addOrUpdateCluster(cluster, version_info)) {
      ENVOY_LOG(debug, "cds: add/update cluster '{}'", cluster_name);
    }
  }

  for (auto cluster : clusters_to_remove) {
    const std::string cluster_name = cluster.first;
    if (cm_.removeCluster(cluster_name)) {
      ENVOY_LOG(debug, "cds: remove cluster '{}'", cluster_name);
    }
  }

  version_info_ = version_info;
  runInitializeCallbackIfAny();
}

void CdsApiImpl::onConfigUpdateFailed(const EnvoyException*) {
  // We need to allow server startup to continue, even if we have a bad
  // config.
  runInitializeCallbackIfAny();
}

void CdsApiImpl::runInitializeCallbackIfAny() {
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

} // namespace Upstream
} // namespace Envoy
