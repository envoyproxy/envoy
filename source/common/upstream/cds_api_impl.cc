#include "common/upstream/cds_api_impl.h"

#include <string>

#include "common/config/subscription_factory.h"
#include "common/config/utility.h"
#include "common/upstream/cds_subscription.h"

namespace Envoy {
namespace Upstream {

CdsApiPtr CdsApiImpl::create(const envoy::api::v2::ConfigSource& cds_config,
                             const Optional<SdsConfig>& sds_config, ClusterManager& cm,
                             Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                             const LocalInfo::LocalInfo& local_info, Stats::Store& store) {
  return CdsApiPtr{
      new CdsApiImpl(cds_config, sds_config, cm, dispatcher, random, local_info, store)};
}

CdsApiImpl::CdsApiImpl(const envoy::api::v2::ConfigSource& cds_config,
                       const Optional<SdsConfig>& sds_config, ClusterManager& cm,
                       Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                       const LocalInfo::LocalInfo& local_info, Stats::Store& store)
    : cm_(cm), scope_(store.createScope("cluster_manager.cds.")) {
  Config::Utility::localInfoToNode(local_info, node_);
  subscription_ =
      Config::SubscriptionFactory::subscriptionFromConfigSource<envoy::api::v2::Cluster>(
          cds_config, node_, dispatcher, cm, random, *scope_,
          [this, &cds_config, &sds_config, &cm, &dispatcher, &random,
           &local_info]() -> Config::Subscription<envoy::api::v2::Cluster>* {
            return new CdsSubscription(Config::Utility::generateStats(*scope_), cds_config,
                                       sds_config, cm, dispatcher, random, local_info);
          },
          "envoy.api.v2.ClusterDiscoveryService.FetchClusters",
          "envoy.api.v2.ClusterDiscoveryService.StreamClusters");
}

void CdsApiImpl::onConfigUpdate(const ResourceVector& resources) {
  // We need to keep track of which clusters we might need to remove.
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
