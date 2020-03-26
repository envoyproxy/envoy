#include "common/upstream/cds_api_impl.h"

#include <string>

#include "envoy/api/v2/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/cleanup.h"
#include "common/common/utility.h"
#include "common/config/api_version.h"
#include "common/config/resources.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Upstream {

CdsApiPtr CdsApiImpl::create(const envoy::config::core::v3::ConfigSource& cds_config,
                             ClusterManager& cm, Stats::Scope& scope,
                             ProtobufMessage::ValidationVisitor& validation_visitor) {
  return CdsApiPtr{new CdsApiImpl(cds_config, cm, scope, validation_visitor)};
}

CdsApiImpl::CdsApiImpl(const envoy::config::core::v3::ConfigSource& cds_config, ClusterManager& cm,
                       Stats::Scope& scope, ProtobufMessage::ValidationVisitor& validation_visitor)
    : cm_(cm), scope_(scope.createScope("cluster_manager.cds.")),
      validation_visitor_(validation_visitor) {
  const auto resource_name = getResourceName(cds_config.resource_api_version());
  subscription_ = cm_.subscriptionFactory().subscriptionFromConfigSource(
      cds_config, Grpc::Common::typeUrl(resource_name), *scope_, *this);
}

void CdsApiImpl::onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                const std::string& version_info) {
  ClusterManager::ClusterInfoMap clusters_to_remove = cm_.clusters();
  std::vector<envoy::config::cluster::v3::Cluster> clusters;
  for (const auto& cluster_blob : resources) {
    // No validation needed here the overloaded call to onConfigUpdate validates.
    clusters.push_back(MessageUtil::anyConvert<envoy::config::cluster::v3::Cluster>(cluster_blob));
    clusters_to_remove.erase(clusters.back().name());
  }
  Protobuf::RepeatedPtrField<std::string> to_remove_repeated;
  for (const auto& cluster : clusters_to_remove) {
    *to_remove_repeated.Add() = cluster.first;
  }
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> to_add_repeated;
  for (const auto& cluster : clusters) {
    envoy::service::discovery::v3::Resource* to_add = to_add_repeated.Add();
    to_add->set_name(cluster.name());
    to_add->set_version(version_info);
    to_add->mutable_resource()->PackFrom(cluster);
  }
  onConfigUpdate(to_add_repeated, to_remove_repeated, version_info);
}

void CdsApiImpl::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  std::unique_ptr<Cleanup> maybe_eds_resume;
  if (cm_.adsMux()) {
    cm_.adsMux()->pause(Config::TypeUrl::get().ClusterLoadAssignment);
    maybe_eds_resume = std::make_unique<Cleanup>(
        [this] { cm_.adsMux()->resume(Config::TypeUrl::get().ClusterLoadAssignment); });
  }

  ENVOY_LOG(info, "cds: add {} cluster(s), remove {} cluster(s)", added_resources.size(),
            removed_resources.size());

  std::vector<std::string> exception_msgs;
  std::unordered_set<std::string> cluster_names;
  bool any_applied = false;
  for (const auto& resource : added_resources) {
    envoy::config::cluster::v3::Cluster cluster;
    try {
      cluster = MessageUtil::anyConvertAndValidate<envoy::config::cluster::v3::Cluster>(
          resource.resource(), validation_visitor_);
      if (!cluster_names.insert(cluster.name()).second) {
        // NOTE: at this point, the first of these duplicates has already been successfully applied.
        throw EnvoyException(fmt::format("duplicate cluster {} found", cluster.name()));
      }
      if (cm_.addOrUpdateCluster(cluster, resource.version())) {
        any_applied = true;
        ENVOY_LOG(info, "cds: add/update cluster '{}'", cluster.name());
      } else {
        ENVOY_LOG(debug, "cds: add/update cluster '{}' skipped", cluster.name());
      }
    } catch (const EnvoyException& e) {
      exception_msgs.push_back(fmt::format("{}: {}", cluster.name(), e.what()));
    }
  }
  for (const auto& resource_name : removed_resources) {
    if (cm_.removeCluster(resource_name)) {
      any_applied = true;
      ENVOY_LOG(info, "cds: remove cluster '{}'", resource_name);
    }
  }

  if (any_applied) {
    system_version_info_ = system_version_info;
  }
  runInitializeCallbackIfAny();
  if (!exception_msgs.empty()) {
    throw EnvoyException(
        fmt::format("Error adding/updating cluster(s) {}", absl::StrJoin(exception_msgs, ", ")));
  }
}

void CdsApiImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                      const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
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
