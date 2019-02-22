#include "common/upstream/cds_api_impl.h"

#include <string>

#include "envoy/api/v2/cds.pb.validate.h"
#include "envoy/api/v2/cluster/outlier_detection.pb.validate.h"
#include "envoy/stats/scope.h"

#include "common/common/cleanup.h"
#include "common/common/utility.h"
#include "common/config/resources.h"
#include "common/config/subscription_factory.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

CdsApiPtr CdsApiImpl::create(const envoy::api::v2::core::ConfigSource& cds_config,
                             ClusterManager& cm, Event::Dispatcher& dispatcher,
                             Runtime::RandomGenerator& random,
                             const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
                             Api::Api& api) {
  return CdsApiPtr{new CdsApiImpl(cds_config, cm, dispatcher, random, local_info, scope, api)};
}

CdsApiImpl::CdsApiImpl(const envoy::api::v2::core::ConfigSource& cds_config, ClusterManager& cm,
                       Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
                       const LocalInfo::LocalInfo& local_info, Stats::Scope& scope, Api::Api& api)
    : cm_(cm), scope_(scope.createScope("cluster_manager.cds.")) {
  Config::Utility::checkLocalInfo("cds", local_info);

  subscription_ =
      Config::SubscriptionFactory::subscriptionFromConfigSource<envoy::api::v2::Cluster>(
          cds_config, local_info, dispatcher, cm, random, *scope_,
          "envoy.api.v2.ClusterDiscoveryService.FetchClusters",
          "envoy.api.v2.ClusterDiscoveryService.StreamClusters", api);
}

void CdsApiImpl::onConfigUpdate(const ResourceVector& resources, const std::string& version_info) {
  cm_.adsMux().pause(Config::TypeUrl::get().ClusterLoadAssignment);
  Cleanup eds_resume([this] { cm_.adsMux().resume(Config::TypeUrl::get().ClusterLoadAssignment); });

  std::vector<std::string> exception_msgs;
  std::unordered_set<std::string> cluster_names;
  for (const auto& cluster : resources) {
    if (!cluster_names.insert(cluster.name()).second) {
      throw EnvoyException(fmt::format("duplicate cluster {} found", cluster.name()));
    }
  }
  for (const auto& cluster : resources) {
    MessageUtil::validate(cluster);
  }
  // We need to keep track of which clusters we might need to remove.
  ClusterManager::ClusterInfoMap clusters_to_remove = cm_.clusters();
  for (auto& cluster : resources) {
    const std::string cluster_name = cluster.name();
    try {
      clusters_to_remove.erase(cluster_name);
      if (cm_.addOrUpdateCluster(
              cluster, version_info,
              [this](const std::string&, ClusterManager::ClusterWarmingState state) {
                // Following if/else block implements a control flow mechanism that can be used
                // by an ADS implementation to properly sequence CDS and RDS update. It is not
                // enforcing on ADS. ADS can use it to detect when a previously sent cluster becomes
                // warm before sending routes that depend on it. This can improve incidence of HTTP
                // 503 responses from Envoy when a route is used before it's supporting cluster is
                // ready.
                //
                // We achieve that by leaving CDS in the paused state as long as there is at least
                // one cluster in the warming state. This prevents CDS ACK from being sent to ADS.
                // Once cluster is warmed up, CDS is resumed, and ACK is sent to ADS, providing a
                // signal to ADS to proceed with RDS updates.
                //
                // Major concern with this approach is CDS being left in the paused state forever.
                // As long as ClusterManager::removeCluster() is not called on a warming cluster
                // this is not an issue. CdsApiImpl takes care of doing this properly, and there
                // is no other component removing clusters from the ClusterManagerImpl. If this
                // ever changes, we would need to correct the following logic.
                if (state == ClusterManager::ClusterWarmingState::Starting &&
                    cm_.warmingClusterCount() == 1) {
                  cm_.adsMux().pause(Config::TypeUrl::get().Cluster);
                } else if (state == ClusterManager::ClusterWarmingState::Finished &&
                           cm_.warmingClusterCount() == 0) {
                  cm_.adsMux().resume(Config::TypeUrl::get().Cluster);
                }
              })) {
        ENVOY_LOG(debug, "cds: add/update cluster '{}'", cluster_name);
      }
    } catch (const EnvoyException& e) {
      exception_msgs.push_back(e.what());
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
  if (!exception_msgs.empty()) {
    throw EnvoyException(StringUtil::join(exception_msgs, "\n"));
  }
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
