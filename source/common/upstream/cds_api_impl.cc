#include "source/common/upstream/cds_api_impl.h"

#include "source/common/common/assert.h"
#include "source/common/grpc/common.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Upstream {

absl::StatusOr<CdsApiPtr>
CdsApiImpl::create(const envoy::config::core::v3::ConfigSource& cds_config,
                   const xds::core::v3::ResourceLocator* cds_resources_locator, ClusterManager& cm,
                   Stats::Scope& scope, ProtobufMessage::ValidationVisitor& validation_visitor,
                   Server::Configuration::ServerFactoryContext& factory_context,
                   bool support_multi_ads_sources) {
  absl::Status creation_status = absl::OkStatus();
  auto ret =
      CdsApiPtr{new CdsApiImpl(cds_config, cds_resources_locator, cm, scope, validation_visitor,
                               factory_context, support_multi_ads_sources, creation_status)};
  RETURN_IF_NOT_OK(creation_status);
  return ret;
}

CdsApiImpl::CdsApiImpl(const envoy::config::core::v3::ConfigSource& cds_config,
                       const xds::core::v3::ResourceLocator* cds_resources_locator,
                       ClusterManager& cm, Stats::Scope& scope,
                       ProtobufMessage::ValidationVisitor& validation_visitor,
                       Server::Configuration::ServerFactoryContext& factory_context,
                       bool support_multi_ads_sources, absl::Status& creation_status)
    : Envoy::Config::SubscriptionBase<envoy::config::cluster::v3::Cluster>(validation_visitor,
                                                                           "name"),
      helper_(cm, factory_context.xdsManager(), "cds"), cm_(cm),
      scope_(scope.createScope("cluster_manager.cds.")), factory_context_(factory_context),
      stats_({ALL_CDS_STATS(POOL_COUNTER(*scope_), POOL_GAUGE(*scope_))}),
      support_multi_ads_sources_(support_multi_ads_sources) {
  const auto resource_name = getResourceName();
  absl::StatusOr<Config::SubscriptionPtr> subscription_or_error;
  if (cds_resources_locator == nullptr) {
    subscription_or_error = cm_.subscriptionFactory().subscriptionFromConfigSource(
        cds_config, Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_, {});
  } else {
    subscription_or_error = cm.subscriptionFactory().collectionSubscriptionFromUrl(
        *cds_resources_locator, cds_config, resource_name, *scope_, *this, resource_decoder_);
  }
  SET_AND_RETURN_IF_NOT_OK(subscription_or_error.status(), creation_status);
  subscription_ = std::move(*subscription_or_error);
}

absl::Status CdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                        const std::string& version_info) {
  // If another source may be adding clusters to the cluster-manager, Envoy needs to
  // track which clusters are received via the SotW CDS configuration, so only
  // clusters that were added through SotW CDS and are not updated will be removed.
  if (support_multi_ads_sources_) {
    // The input resources will be the next sotw_resource_names_.
    absl::flat_hash_set<std::string> next_sotw_resource_names;
    next_sotw_resource_names.reserve(resources.size());
    std::transform(resources.cbegin(), resources.cend(),
                   std::inserter(next_sotw_resource_names, next_sotw_resource_names.begin()),
                   [](const Config::DecodedResourceRef resource) { return resource.get().name(); });
    // Find all the clusters that are currently used, but no longer appear in
    // the next step.
    Protobuf::RepeatedPtrField<std::string> to_remove;
    for (const std::string& cluster_name : sotw_resource_names_) {
      if (!next_sotw_resource_names.contains(cluster_name)) {
        to_remove.Add(std::string(cluster_name));
      }
    }
    absl::Status status = onConfigUpdate(resources, to_remove, version_info);
    // Even if the onConfigUpdate() above returns an error, some of the clusters
    // may have been updated. Either way, we use the new update to override the
    // contents.
    // TODO(adisuissa): This will not be needed once the xDS-Cache layer is
    // introduced, as it will keep track of only the valid resources.
    sotw_resource_names_ = std::move(next_sotw_resource_names);
    return status;
  }

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
  auto [added_or_updated, exception_msgs] =
      helper_.onConfigUpdate(added_resources, removed_resources, system_version_info);
  runInitializeCallbackIfAny();
  if (!exception_msgs.empty()) {
    return absl::InvalidArgumentError(
        fmt::format("Error adding/updating cluster(s) {}", absl::StrJoin(exception_msgs, ", ")));
  }
  if (added_or_updated > 0) {
    stats_.config_reload_.inc();
    stats_.config_reload_time_ms_.set(DateUtil::nowToMilliseconds(factory_context_.timeSource()));
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
