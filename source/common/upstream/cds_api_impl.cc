#include "source/common/upstream/cds_api_impl.h"

#include "source/common/common/assert.h"
#include "source/common/grpc/common.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Upstream {

CdsApiPtr CdsApiImpl::create(const envoy::config::core::v3::ConfigSource& cds_config,
                             const xds::core::v3::ResourceLocator* cds_resources_locator,
                             ClusterManager& cm, Stats::Scope& scope,
                             ProtobufMessage::ValidationVisitor& validation_visitor) {
  return CdsApiPtr{
      new CdsApiImpl(cds_config, cds_resources_locator, cm, scope, validation_visitor)};
}

CdsApiImpl::CdsApiImpl(const envoy::config::core::v3::ConfigSource& cds_config,
                       const xds::core::v3::ResourceLocator* cds_resources_locator,
                       ClusterManager& cm, Stats::Scope& scope,
                       ProtobufMessage::ValidationVisitor& validation_visitor)
    : Envoy::Config::SubscriptionBase<envoy::config::cluster::v3::Cluster>(validation_visitor,
                                                                           "name"),
      helper_(cm, "cds"), cm_(cm), scope_(scope.createScope("cluster_manager.cds.")) {
  const auto resource_name = getResourceName();
  if (cds_resources_locator == nullptr) {
    subscription_ = THROW_OR_RETURN_VALUE(cm_.subscriptionFactory().subscriptionFromConfigSource(
                                              cds_config, Grpc::Common::typeUrl(resource_name),
                                              *scope_, *this, resource_decoder_, {}),
                                          Config::SubscriptionPtr);
  } else {
    subscription_ = THROW_OR_RETURN_VALUE(
        cm.subscriptionFactory().collectionSubscriptionFromUrl(
            *cds_resources_locator, cds_config, resource_name, *scope_, *this, resource_decoder_),
        Config::SubscriptionPtr);
  }
}

absl::Status CdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                        const std::string& version_info) {
  auto all_existing_clusters = cm_.clusters();
  // Exclude the clusters which CDS wants to add.
  for (const auto& resource : resources) {
    all_existing_clusters.active_clusters_.erase(resource.get().name());
    all_existing_clusters.warming_clusters_.erase(resource.get().name());
  }
  Protobuf::RepeatedPtrField<std::string> to_remove_repeated;
  for (const auto& [cluster_name, _] : all_existing_clusters.active_clusters_) {
    UNREFERENCED_PARAMETER(_);
    *to_remove_repeated.Add() = cluster_name;
  }
  for (const auto& [cluster_name, _] : all_existing_clusters.warming_clusters_) {
    UNREFERENCED_PARAMETER(_);
    // Do not add the cluster twice when the cluster is both active and warming.
    if (!all_existing_clusters.active_clusters_.contains(cluster_name)) {
      *to_remove_repeated.Add() = cluster_name;
    }
  }
  return onConfigUpdate(resources, to_remove_repeated, version_info);
}

absl::Status
CdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                           const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                           const std::string& system_version_info) {
  auto exception_msgs =
      helper_.onConfigUpdate(added_resources, removed_resources, system_version_info);
  runInitializeCallbackIfAny();
  if (!exception_msgs.empty()) {
    return absl::InvalidArgumentError(
        fmt::format("Error adding/updating cluster(s) {}", absl::StrJoin(exception_msgs, ", ")));
  }
  return absl::OkStatus();
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
