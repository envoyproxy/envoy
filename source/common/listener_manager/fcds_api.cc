#include "source/common/listener_manager/fcds_api.h"

#include "source/common/config/xds_resource.h"
#include "source/common/grpc/common.h"

namespace Envoy {
namespace Server {

FcdsApiImpl::FcdsApiImpl(const envoy::config::core::v3::ConfigSource& fcds_config,
                         absl::string_view resources_locator, absl::string_view listener_name,
                         Upstream::ClusterManager& cluster_manager, Stats::Scope& scope,
                         Init::Manager& init_manager, ListenerManager& listener_manager,
                         ProtobufMessage::ValidationVisitor& validation_visitor)
    : Envoy::Config::SubscriptionBase<envoy::config::listener::v3::FilterChain>(validation_visitor,
                                                                                "name"),
      resources_locator_(resources_locator), listener_name_(listener_name),
      scope_(scope.createScope("fcds.")), listener_manager_(listener_manager),
      local_init_target_("FCDS-local",
                         [this]() {
                           // This init target that is used to start the subscription regardless of
                           // whether the listener is started with warming or not.
                           subscription_->start({});
                           local_init_target_.ready();
                         }),
      init_target_("FCDS", []() {}) {
  const xds::core::v3::ResourceLocator fcds_resource_locator = THROW_OR_RETURN_VALUE(
      Config::XdsResourceIdentifier::decodeUrl(resources_locator_), xds::core::v3::ResourceLocator);
  const auto resource_name = getResourceName();
  subscription_ = THROW_OR_RETURN_VALUE(
      cluster_manager.subscriptionFactory().collectionSubscriptionFromUrl(
          fcds_resource_locator, fcds_config, resource_name, *scope_, *this, resource_decoder_),
      Config::SubscriptionPtr);

  init_manager.add(local_init_target_);
}

absl::Status
FcdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                            const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                            const std::string& system_version_info) {
  std::string error_message;

  absl::optional<absl::string_view> version = absl::nullopt;
  if (!added_resources.empty()) {
    version = added_resources[0].get().version();
  }

  FilterChainRefVector added_filter_chains;
  added_filter_chains.reserve(added_resources.size());
  for (const auto& resource : added_resources) {
    if (!resource.get().hasResource()) {
      continue;
    }

    added_filter_chains.push_back(
        dynamic_cast<const envoy::config::listener::v3::FilterChain&>(resource.get().resource()));
  }

  absl::flat_hash_set<absl::string_view> removed_filter_chains;
  removed_filter_chains.reserve(removed_resources.size());
  for (const auto& resource : removed_resources) {
    removed_filter_chains.insert(absl::string_view(resource));
  }

  TRY_ASSERT_MAIN_THREAD {
    absl::Status update_or_error = listener_manager_.updateDynamicFilterChains(
        listener_name_, version, added_filter_chains, removed_filter_chains);

    if (!update_or_error.ok()) {
      error_message = std::string(update_or_error.message());
    } else {
      ENVOY_LOG(info, "fcds: updated listener '{}' filter chains", listener_name_);
    }
  }
  END_TRY
  CATCH(EnvoyException & e, { error_message = e.what(); })

  init_target_.ready();

  if (!error_message.empty()) {
    return absl::InvalidArgumentError(
        fmt::format("Error updating listener {} with FCDS: {}", listener_name_, error_message));
  }

  system_version_info_ = system_version_info;
  return absl::OkStatus();
}

absl::Status FcdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>&,
                                         const std::string&) {
  return absl::UnavailableError("SoTW FCDS is not implemented");
}

void FcdsApiImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                       const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad config.
  init_target_.ready();
}

} // namespace Server
} // namespace Envoy
