#include "source/common/upstream/od_cds_api_impl.h"

#include "source/common/common/assert.h"
#include "source/common/grpc/common.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Upstream {

OdCdsApiSharedPtr
OdCdsApiImpl::create(const envoy::config::core::v3::ConfigSource& odcds_config,
                     OptRef<xds::core::v3::ResourceLocator> odcds_resources_locator,
                     ClusterManager& cm, MissingClusterNotifier& notifier, Stats::Scope& scope,
                     ProtobufMessage::ValidationVisitor& validation_visitor) {
  return OdCdsApiSharedPtr(new OdCdsApiImpl(odcds_config, odcds_resources_locator, cm, notifier,
                                            scope, validation_visitor));
}

OdCdsApiImpl::OdCdsApiImpl(const envoy::config::core::v3::ConfigSource& odcds_config,
                           OptRef<xds::core::v3::ResourceLocator> odcds_resources_locator,
                           ClusterManager& cm, MissingClusterNotifier& notifier,
                           Stats::Scope& scope,
                           ProtobufMessage::ValidationVisitor& validation_visitor)
    : Envoy::Config::SubscriptionBase<envoy::config::cluster::v3::Cluster>(validation_visitor,
                                                                           "name"),
      helper_(cm, "odcds"), cm_(cm), notifier_(notifier),
      scope_(scope.createScope("cluster_manager.odcds.")), status_(StartStatus::NotStarted) {
  // TODO(krnowak): Move the subscription setup to CdsApiHelper. Maybe make CdsApiHelper a base
  // class for CDS and ODCDS.
  const auto resource_name = getResourceName();
  if (!odcds_resources_locator.has_value()) {
    subscription_ = cm_.subscriptionFactory().subscriptionFromConfigSource(
        odcds_config, Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_, {});
  } else {
    subscription_ = cm.subscriptionFactory().collectionSubscriptionFromUrl(
        *odcds_resources_locator, odcds_config, resource_name, *scope_, *this, resource_decoder_);
  }
}

void OdCdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                  const std::string& version_info) {
  UNREFERENCED_PARAMETER(resources);
  UNREFERENCED_PARAMETER(version_info);
  // On-demand cluster updates are only supported for delta, not sotw.
  PANIC("not supported");
}

void OdCdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                                  const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                                  const std::string& system_version_info) {
  auto exception_msgs =
      helper_.onConfigUpdate(added_resources, removed_resources, system_version_info);
  if (status_ != StartStatus::InitialFetchDone) {
    sendAwaiting();
    status_ = StartStatus::InitialFetchDone;
  }
  // According to the XDS specification, the server can send a reply with names in the
  // removed_resources field for requested resources that do not exist. That way we can notify the
  // interested parties about the missing resource immediately without waiting for some timeout to
  // be triggered.
  for (const auto& resource_name : removed_resources) {
    ENVOY_LOG(debug, "odcds: notifying about potential missing cluster {}", resource_name);
    notifier_.notifyMissingCluster(resource_name);
  }
  if (!exception_msgs.empty()) {
    throw EnvoyException(
        fmt::format("Error adding/updating cluster(s) {}", absl::StrJoin(exception_msgs, ", ")));
  }
}

void OdCdsApiImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                        const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  if (status_ != StartStatus::InitialFetchDone) {
    sendAwaiting();
    status_ = StartStatus::InitialFetchDone;
  }
}

void OdCdsApiImpl::sendAwaiting() {
  // skip it when there is only the init fetch cluster.
  if (watched_resources_.size() > 1) {
    ENVOY_LOG(debug, "odcds: updating watched cluster names {}",
              fmt::join(watched_resources_, ", "));
    subscription_->updateResourceInterest(watched_resources_);
  }
}

void OdCdsApiImpl::updateOnDemand(std::string cluster_name) {
  switch (status_) {
  case StartStatus::NotStarted:
    ENVOY_LOG(trace, "odcds: starting a subscription with cluster name {}", cluster_name);
    status_ = StartStatus::Started;
    watched_resources_.insert(cluster_name);
    subscription_->start({std::move(cluster_name)});
    return;

  case StartStatus::Started:
    ENVOY_LOG(trace, "odcds: putting cluster name {} on awaiting list", cluster_name);
    watched_resources_.insert(std::move(cluster_name));
    return;

  case StartStatus::InitialFetchDone:
    auto old_size = watched_resources_.size();
    watched_resources_.insert(cluster_name);
    if (watched_resources_.size() != old_size) {
      ENVOY_LOG(debug, "odcds: updating watched cluster names {}",
                fmt::join(watched_resources_, ", "));
      subscription_->updateResourceInterest(watched_resources_);
    } else {
      ENVOY_LOG(trace, "odcds: requesting for cluster name {}", cluster_name);
      subscription_->requestOnDemandUpdate({std::move(cluster_name)});
    }

    return;
  }
  PANIC("corrupt enum");
}

} // namespace Upstream
} // namespace Envoy
