#include "common/upstream/cds_api_delta_impl.h"

#include <string>

#include "envoy/api/v2/cds.pb.validate.h"
#include "envoy/api/v2/cluster/outlier_detection.pb.validate.h"
#include "envoy/stats/scope.h"

#include "common/common/cleanup.h"
<<<<<<< HEAD
#include "common/config/resources.h"
#include "common/config/subscription_factory.h"
=======
#include "common/config/incremental_subscription_factory.h"
#include "common/config/resources.h"
>>>>>>> snapshot
#include "common/config/utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

<<<<<<< HEAD:source/common/upstream/cds_api_incremental_impl.cc
<<<<<<< HEAD
=======
<<<<<<<< HEAD:source/common/upstream/cds_api_incremental_impl.cc
>>>>>>> snapshot
CdsApiPtr CdsApiIncrementalImpl::create(const envoy::api::v2::core::ConfigSource& cds_config,
                                        ClusterManager& cm, Event::Dispatcher& dispatcher,
                                        Runtime::RandomGenerator& random,
                                        const LocalInfo::LocalInfo& local_info,
                                        Stats::Scope& scope) {
  return CdsApiPtr{
      new CdsApiIncrementalImpl(cds_config, cm, dispatcher, random, local_info, scope)};
<<<<<<< HEAD
=======
========
CdsApiPtr CdsIncremental::create(const envoy::api::v2::core::ConfigSource& cds_config,
                                 const absl::optional<envoy::api::v2::core::ConfigSource>&,
                                 ClusterManager& cm, Event::Dispatcher& dispatcher,
                                 Runtime::RandomGenerator& random,
                                 const LocalInfo::LocalInfo& local_info, Stats::Scope& scope) {
  return CdsApiPtr{new CdsIncremental(cds_config, cm, dispatcher, random, local_info, scope)};
>>>>>>>> snapshot:source/common/upstream/cds_incremental.cc
>>>>>>> snapshot
}

CdsApiIncrementalImpl::CdsApiIncrementalImpl(const envoy::api::v2::core::ConfigSource& cds_config,
                                             ClusterManager& cm, Event::Dispatcher& dispatcher,
                                             Runtime::RandomGenerator& random,
                                             const LocalInfo::LocalInfo& local_info,
                                             Stats::Scope& scope)
=======
CdsApiPtr CdsApiDeltaImpl::create(const envoy::api::v2::core::ConfigSource& cds_config,
                                  ClusterManager& cm, Event::Dispatcher& dispatcher,
                                  Runtime::RandomGenerator& random,
                                  const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
                                  Api::Api& api) {
  return CdsApiPtr{new CdsApiDeltaImpl(cds_config, cm, dispatcher, random, local_info, scope, api)};
}

CdsApiDeltaImpl::CdsApiDeltaImpl(const envoy::api::v2::core::ConfigSource& cds_config,
                                 ClusterManager& cm, Event::Dispatcher& dispatcher,
                                 Runtime::RandomGenerator& random,
                                 const LocalInfo::LocalInfo& local_info, Stats::Scope& scope,
                                 Api::Api& api)
>>>>>>> rename incremental to delta:source/common/upstream/cds_api_delta_impl.cc
    : cm_(cm), scope_(scope.createScope("cluster_manager.cds.")) {
  Config::Utility::checkLocalInfo("cds", local_info);
  subscription_ =
<<<<<<< HEAD
      Config::SubscriptionFactory::subscriptionFromConfigSource<envoy::api::v2::Cluster>(
          cds_config, local_info, dispatcher, cm, random, *scope_, "not implemented",
<<<<<<< HEAD:source/common/upstream/cds_api_incremental_impl.cc
          "envoy.api.v2.ClusterDiscoveryService.IncrementalClusters");
}

void CdsApiIncrementalImpl::onConfigUpdate(
=======
      Config::IncrementalSubscriptionFactory::subscriptionFromConfigSource<envoy::api::v2::Cluster>(
          cds_config, local_info, dispatcher, cm, random, *scope_,
          "envoy.api.v2.ClusterDiscoveryService.IncrementalClusters");
}

void CdsApiIncrementalImpl::onIncrementalConfigUpdate(
>>>>>>> snapshot
=======
          "envoy.api.v2.ClusterDiscoveryService.DeltaClusters", api);
}

void CdsApiDeltaImpl::onConfigUpdate(
>>>>>>> rename incremental to delta:source/common/upstream/cds_api_delta_impl.cc
    const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  cm_.adsMux().pause(Config::TypeUrl::get().ClusterLoadAssignment);
  Cleanup eds_resume([this] { cm_.adsMux().resume(Config::TypeUrl::get().ClusterLoadAssignment); });

  for (const auto& resource : added_resources) {
<<<<<<< HEAD
    try {
      envoy::api::v2::Cluster cluster =
          MessageUtil::anyConvert<envoy::api::v2::Cluster>(resource.resource());
      MessageUtil::validate(cluster);
      if (cm_.addOrUpdateCluster(cluster, resource.version())) {
        ENVOY_LOG(debug, "cds: add/update cluster '{}'", cluster.name());
      }
    } catch (const EnvoyException& e) {
      // TODO TODO add this resource to the "resource updates that had problems" pile, to go into a
      // larger partial rejection exception
      std::cerr << "TODO TODO had a problem with a resource" << std::endl;
=======
    envoy::api::v2::Cluster cluster =
        MessageUtil::anyConvert<envoy::api::v2::Cluster>(resource.resource());
    MessageUtil::validate(cluster);
    if (cm_.addOrUpdateCluster(cluster, resource.version())) {
      ENVOY_LOG(debug, "cds: add/update cluster '{}'", cluster.name());
>>>>>>> snapshot
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

<<<<<<< HEAD:source/common/upstream/cds_api_incremental_impl.cc
<<<<<<< HEAD
void CdsApiIncrementalImpl::onConfigUpdateFailed(const EnvoyException*) {
=======
void CdsApiIncrementalImpl::onIncrementalConfigUpdateFailed(const EnvoyException*) {
>>>>>>> snapshot
=======
void CdsApiDeltaImpl::onConfigUpdateFailed(const EnvoyException*) {
>>>>>>> rename incremental to delta:source/common/upstream/cds_api_delta_impl.cc
  // We need to allow server startup to continue, even if we have a bad config.
  runInitializeCallbackIfAny();
}

<<<<<<< HEAD:source/common/upstream/cds_api_incremental_impl.cc
<<<<<<< HEAD
=======
std::set<std::string> CdsApiIncrementalImpl::clusterNames() {
  std::set<std::string> cluster_names;
  for (const auto& c : cm_.clusters()) {
    cluster_names.insert(c.first);
  }
  return cluster_names;
}

>>>>>>> snapshot
void CdsApiIncrementalImpl::runInitializeCallbackIfAny() {
=======
void CdsApiDeltaImpl::runInitializeCallbackIfAny() {
>>>>>>> rename incremental to delta:source/common/upstream/cds_api_delta_impl.cc
  if (initialize_callback_) {
    initialize_callback_();
    initialize_callback_ = nullptr;
  }
}

} // namespace Upstream
} // namespace Envoy
