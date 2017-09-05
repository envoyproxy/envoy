#include "common/upstream/cds_api_impl.h"

#include <string>

#include "common/config/subscription_factory.h"
#include "common/config/utility.h"
#include "common/upstream/cds_subscription.h"

namespace Envoy {
namespace Upstream {

CdsApiPtr CdsApiImpl::create(const envoy::api::v2::ConfigSource& cds_config,
                             const Optional<envoy::api::v2::ConfigSource>& eds_config,
                             ClusterManager& cm, Event::Dispatcher& dispatcher,
                             Runtime::RandomGenerator& random,
                             const LocalInfo::LocalInfo& local_info, Stats::Scope& scope) {
  return CdsApiPtr{
      new CdsApiImpl(cds_config, eds_config, cm, dispatcher, random, local_info, scope)};
}

CdsApiImpl::CdsApiImpl(const envoy::api::v2::ConfigSource& cds_config,
                       const Optional<envoy::api::v2::ConfigSource>& eds_config, ClusterManager& cm,
                       Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                       const LocalInfo::LocalInfo& local_info, Stats::Scope& scope)
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

void CdsApiImpl::onConfigUpdate(std::string version_info, const ResourceVector& resources) {
  // We need to keep track of which clusters we might need to remove.
  // TODO(dhochman): store version info
  UNREFERENCED_PARAMETER(version_info);
  ClusterManager::ClusterInfoMap clusters_to_remove = cm_.clusters();
  for (auto& cluster : resources) {
    const std::string cluster_name = cluster.name();
    clusters_to_remove.erase(cluster_name);
    if (cm_.addOrUpdatePrimaryCluster(cluster)) {
      ENVOY_LOG(info, "cds: add/update cluster '{}'", cluster_name);
    }
  }

  for (auto cluster : clusters_to_remove) {
    if (cm_.removePrimaryCluster(cluster.first)) {
      ENVOY_LOG(info, "cds: remove cluster '{}'", cluster.first);
    }
  }

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
