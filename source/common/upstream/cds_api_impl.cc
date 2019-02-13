#include "common/upstream/cds_api_impl.h"

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

CdsApiPtr CdsApiImpl::create(const envoy::api::v2::core::ConfigSource& cds_config,
                             ClusterManager& cm, Event::Dispatcher& dispatcher,
                             Runtime::RandomGenerator& random,
                             const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
                             Api::Api& api) {
  return CdsApiPtr{new CdsApiImpl(cds_config, cm, dispatcher, random, local_info, scope, api)};
}

CdsApiImpl::CdsApiImpl(const envoy::api::v2::core::ConfigSource& cds_config, ClusterManager& cm,
                       Event::Dispatcher& dispatcher, Runtime::RandomGenerator& random,
<<<<<<< HEAD
                       const LocalInfo::LocalInfo& local_info, Stats::Scope& scope)
    : CdsApiIncrementalImpl(cds_config, cm, dispatcher, random, local_info, scope) {}
=======
                       const LocalInfo::LocalInfo& local_info, Stats::Scope& scope, Api::Api& api)
    : cm_(cm), scope_(scope.createScope("cluster_manager.cds.")) {
  Config::Utility::checkLocalInfo("cds", local_info);

  subscription_ =
      Config::SubscriptionFactory::subscriptionFromConfigSource<envoy::api::v2::Cluster>(
          cds_config, local_info, dispatcher, cm, random, *scope_,
          "envoy.api.v2.ClusterDiscoveryService.FetchClusters",
          "envoy.api.v2.ClusterDiscoveryService.DeltaClusters", api);
}

void CdsApiImpl::onConfigUpdate(const ResourceVector& resources, const std::string& version_info) {
  cm_.adsMux().pause(Config::TypeUrl::get().ClusterLoadAssignment);
  Cleanup eds_resume([this] { cm_.adsMux().resume(Config::TypeUrl::get().ClusterLoadAssignment); });
>>>>>>> filesystem: convert free functions to object methods (#5692)

// TODO(fredlas) use ResourceVector typedef once all xDS have incremental implemented,
//               so that we can rely on IncrementalSubscriptionCallbacks to provide the typedef.
void CdsApiImpl::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::api::v2::Cluster>& resources,
    const std::string& version_info) {
  // Validation: guard against duplicates, and validate each Cluster proto.
  std::unordered_set<std::string> cluster_names;
  for (const auto& cluster : resources) {
    if (!cluster_names.insert(cluster.name()).second) {
      throw EnvoyException(fmt::format("duplicate cluster {} found", cluster.name()));
    }
  }
  for (const auto& cluster : resources) {
    MessageUtil::validate(cluster);
  }

  Protobuf::RepeatedPtrField<envoy::api::v2::Resource> added_clusters;
  Protobuf::RepeatedPtrField<std::string> removed_clusters;
  std::set<std::string> clusters_to_remove = clusterNames();
  for (auto& cluster : resources) {
    envoy::api::v2::Resource added;
    added.set_version(version_info);
    added.mutable_resource()->PackFrom(cluster);
    *added_clusters.Add() = added;
    const std::string cluster_name = cluster.name();
    clusters_to_remove.erase(cluster_name);
  }
  for (auto cluster : clusters_to_remove) {
    *removed_clusters.Add() = cluster;
  }

<<<<<<< HEAD
  onIncrementalConfigUpdate(added_clusters, removed_clusters, version_info);
=======
  whole_update_version_info_ = version_info;
  runInitializeCallbackIfAny();
}

void CdsApiImpl::onConfigUpdate(
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
    // TODO(fredlas) catch exceptions from this block, and add the offending resource to a "resource
    // updates that had problems" pile, to go into a larger partial rejection exception.
  }
  for (auto resource_name : removed_resources) {
    if (cm_.removeCluster(resource_name)) {
      ENVOY_LOG(debug, "cds: remove cluster '{}'", resource_name);
    }
  }

  whole_update_version_info_ = system_version_info;
  runInitializeCallbackIfAny();
>>>>>>> consolidate CdsApiImpl
}

void CdsApiImpl::onConfigUpdateFailed(const EnvoyException*) {
  // We need to allow server startup to continue, even if we have a bad config.
  runInitializeCallbackIfAny();
}

} // namespace Upstream
} // namespace Envoy
