#include "common/upstream/od_cds_api_impl.h"

#include "common/common/assert.h"
#include "common/grpc/common.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Upstream {

OdCdsApiPtr OdCdsApiImpl::create(const envoy::config::core::v3::ConfigSource& odcds_config,
                                 OptRef<xds::core::v3::ResourceLocator> odcds_resources_locator,
                                 ClusterManager& cm, Stats::Scope& scope,
                                 ProtobufMessage::ValidationVisitor& validation_visitor) {
  return OdCdsApiPtr(
      new OdCdsApiImpl(odcds_config, odcds_resources_locator, cm, scope, validation_visitor));
}

OdCdsApiImpl::OdCdsApiImpl(const envoy::config::core::v3::ConfigSource& odcds_config,
                           OptRef<xds::core::v3::ResourceLocator> odcds_resources_locator,
                           ClusterManager& cm, Stats::Scope& scope,
                           ProtobufMessage::ValidationVisitor& validation_visitor)
    : Envoy::Config::SubscriptionBase<envoy::config::cluster::v3::Cluster>(
          odcds_config.resource_api_version(), validation_visitor, "name"),
      helper_(cm, "odcds"), cm_(cm), scope_(scope.createScope("odcds.")),
      status_(StartStatus::NotStarted) {
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
  NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
}

void OdCdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                                  const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                                  const std::string& system_version_info) {
  auto exception_msgs =
      helper_.onConfigUpdate(added_resources, removed_resources, system_version_info);
  sendAwaiting();
  if (!exception_msgs.empty()) {
    throw EnvoyException(
        fmt::format("Error adding/updating cluster(s) {}", absl::StrJoin(exception_msgs, ", ")));
  }
}

void OdCdsApiImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                        const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  sendAwaiting();
}

void OdCdsApiImpl::sendAwaiting() {
  if (status_ != StartStatus::Started) {
    return;
  }
  status_ = StartStatus::InitialFetchDone;
  if (awaiting_names_.empty()) {
    return;
  }
  subscription_->requestOnDemandUpdate(awaiting_names_);
  awaiting_names_.clear();
}

void OdCdsApiImpl::updateOnDemand(const std::string& cluster_name) {
  switch (status_) {
  case StartStatus::NotStarted:
    status_ = StartStatus::Started;
    subscription_->start({cluster_name});
    return;

  case StartStatus::Started:
    awaiting_names_.insert(cluster_name);
    return;

  case StartStatus::InitialFetchDone:
    subscription_->requestOnDemandUpdate({cluster_name});
    return;
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

} // namespace Upstream
} // namespace Envoy
