#include "common/upstream/odcds_api_impl.h"

#include "common/common/assert.h"
#include "common/grpc/common.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Upstream {
namespace {

constexpr int maxTries = 3;

} // namespace

OdCdsApiPtr OdCdsApiImpl::create(const envoy::config::core::v3::ConfigSource& cds_config,
                                 ClusterManager& cm, Stats::Scope& scope,
                                 ProtobufMessage::ValidationVisitor& validation_visitor) {
  return OdCdsApiPtr{new OdCdsApiImpl(cds_config, cm, scope, validation_visitor)};
}

OdCdsApiImpl::OdCdsApiImpl(const envoy::config::core::v3::ConfigSource& cds_config,
                           ClusterManager& cm, Stats::Scope& scope,
                           ProtobufMessage::ValidationVisitor& validation_visitor)
    : Envoy::Config::SubscriptionBase<envoy::config::cluster::v3::Cluster>(
          cds_config.resource_api_version(), validation_visitor, "name"),
      helper_(cm, "odcds"), cm_(cm), scope_(scope.createScope("odcds.")),
      status_(StartStatus::NotStarted) {
  const auto resource_name = getResourceName();
  // TODO(krnowak): If on-demand filter config is indeed created
  // outside the main thread (thus OdCdsApiImpl too), then move the
  // subscription creation to updateOnDemand - the function is only
  // called in main thread.
  subscription_ = cm_.subscriptionFactory().subscriptionFromConfigSource(
      cds_config, Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_, {});
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
  auto supposed_to_be_discovered = std::move(discovered_names_);
  discovered_names_.clear();
  sendAwaiting();
  // first drop all the actually discovered names from
  // supposed_to_be_discovered map
  for (const auto& resource : added_resources) {
    auto cluster_ptr =
        dynamic_cast<const envoy::config::cluster::v3::Cluster*>(&resource.get().resource());
    if (cluster_ptr != nullptr) {
      supposed_to_be_discovered.erase(cluster_ptr->name());
    }
  }
  // If we got maxTries replies without the name we wanted to
  // discover, then assume that there is no such cluster - notify the
  // thread about this fact.
  for (auto [name, tries_left] : supposed_to_be_discovered) {
    auto new_tries_left = tries_left - 1;
    if (new_tries_left > 0) {
      discovered_names_.insert({std::move(name), new_tries_left});
    } else {
      notify(name);
    }
  }
  if (!exception_msgs.empty()) {
    throw EnvoyException(
        fmt::format("Error adding/updating cluster(s) {}", absl::StrJoin(exception_msgs, ", ")));
  }
}

void OdCdsApiImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                        const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  auto supposed_to_be_discovered = std::move(discovered_names_);
  discovered_names_.clear();
  sendAwaiting();
  for (auto [cluster_name, _] : supposed_to_be_discovered) {
    UNREFERENCED_PARAMETER(_);
    notify(cluster_name);
  }
}

void OdCdsApiImpl::sendAwaiting() {
  if (status_ == StartStatus::Started) {
    status_ = StartStatus::InitialFetchDone;
    if (!awaiting_names_.empty()) {
      subscription_->requestOnDemandUpdate(awaiting_names_);
      for (auto& name : awaiting_names_) {
        discovered_names_.insert({std::move(name), maxTries});
      }
      awaiting_names_.clear();
    }
  }
}

void OdCdsApiImpl::notify(const std::string& cluster_name) {
  const bool cluster_exists = false;
  cm_.notifyOnDemandCluster(cluster_name, cluster_exists);
}

void OdCdsApiImpl::updateOnDemand(const std::string& cluster_name) {
  switch (status_) {
  case StartStatus::NotStarted:
    status_ = StartStatus::Started;
    discovered_names_.insert({cluster_name, maxTries});
    subscription_->start({cluster_name});
    return;

  case StartStatus::Started:
    awaiting_names_.insert(cluster_name);
    return;

  case StartStatus::InitialFetchDone:
    discovered_names_.insert({cluster_name, maxTries});
    subscription_->requestOnDemandUpdate({cluster_name});
    return;
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

} // namespace Upstream
} // namespace Envoy
