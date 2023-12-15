#include "source/common/listener_manager/lds_api.h"

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/cleanup.h"
#include "source/common/config/api_version.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/node_hash_set.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Server {

LdsApiImpl::LdsApiImpl(const envoy::config::core::v3::ConfigSource& lds_config,
                       const xds::core::v3::ResourceLocator* lds_resources_locator,
                       Upstream::ClusterManager& cm, Init::Manager& init_manager,
                       Stats::Scope& scope, ListenerManager& lm,
                       ProtobufMessage::ValidationVisitor& validation_visitor)
    : Envoy::Config::SubscriptionBase<envoy::config::listener::v3::Listener>(validation_visitor,
                                                                             "name"),
      listener_manager_(lm), scope_(scope.createScope("listener_manager.lds.")), cm_(cm),
      init_target_("LDS", [this]() { subscription_->start({}); }) {
  const auto resource_name = getResourceName();
  if (lds_resources_locator == nullptr) {
    subscription_ = cm.subscriptionFactory().subscriptionFromConfigSource(
        lds_config, Grpc::Common::typeUrl(resource_name), *scope_, *this, resource_decoder_, {});
  } else {
    subscription_ = cm.subscriptionFactory().collectionSubscriptionFromUrl(
        *lds_resources_locator, lds_config, resource_name, *scope_, *this, resource_decoder_);
  }
  init_manager.add(init_target_);
}

absl::Status
LdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                           const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                           const std::string& system_version_info) {
  Config::ScopedResume maybe_resume_rds_sds;
  if (cm_.adsMux()) {
    const std::vector<std::string> paused_xds_types{
        Config::getTypeUrl<envoy::config::route::v3::RouteConfiguration>(),
        Config::getTypeUrl<envoy::config::route::v3::ScopedRouteConfiguration>(),
        Config::getTypeUrl<envoy::extensions::transport_sockets::tls::v3::Secret>()};
    maybe_resume_rds_sds = cm_.adsMux()->pause(paused_xds_types);
  }

  bool any_applied = false;
  listener_manager_.beginListenerUpdate();

  // We do all listener removals before adding the new listeners. This allows adding a new
  // listener with the same address as a listener that is to be removed. Do not change the order.
  for (const auto& removed_listener : removed_resources) {
    if (listener_manager_.removeListener(removed_listener)) {
      ENVOY_LOG(info, "lds: remove listener '{}'", removed_listener);
      any_applied = true;
    }
  }

  ListenerManager::FailureStates failure_state;
  absl::node_hash_set<std::string> listener_names;
  std::string message;

  for (const auto& resource : added_resources) {
    envoy::config::listener::v3::Listener listener;

    auto onError = [&](std::string error_message) {
      failure_state.push_back(std::make_unique<envoy::admin::v3::UpdateFailureState>());
      auto& state = failure_state.back();
      state->set_details(error_message);
      state->mutable_failed_configuration()->PackFrom(resource.get().resource());
      absl::StrAppend(&message, listener.name(), ": ", error_message, "\n");
    };

    TRY_ASSERT_MAIN_THREAD {
      listener =
          dynamic_cast<const envoy::config::listener::v3::Listener&>(resource.get().resource());
      if (!listener_names.insert(listener.name()).second) {
        // NOTE: at this point, the first of these duplicates has already been successfully
        // applied.
        onError(fmt::format("duplicate listener {} found", listener.name()));
        continue;
      }
      absl::StatusOr<bool> update_or_error =
          listener_manager_.addOrUpdateListener(listener, resource.get().version(), true);
      if (!update_or_error.status().ok()) {
        onError(std::string(update_or_error.status().message()));
        continue;
      }
      if (update_or_error.value()) {
        ENVOY_LOG(info, "lds: add/update listener '{}'", listener.name());
        any_applied = true;
      } else {
        ENVOY_LOG(debug, "lds: add/update listener '{}' skipped", listener.name());
      }
    }
    END_TRY
    catch (const EnvoyException& e) {
      onError(e.what());
    }
  }
  listener_manager_.endListenerUpdate(std::move(failure_state));

  if (any_applied) {
    system_version_info_ = system_version_info;
  }
  init_target_.ready();
  if (!message.empty()) {
    return absl::InvalidArgumentError(fmt::format("Error adding/updating listener(s) {}", message));
  }
  return absl::OkStatus();
}

absl::Status LdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                        const std::string& version_info) {
  // We need to keep track of which listeners need to remove.
  // Specifically, it's [listeners we currently have] - [listeners found in the response].
  absl::node_hash_set<std::string> listeners_to_remove;
  for (const auto& listener :
       listener_manager_.listeners(ListenerManager::WARMING | ListenerManager::ACTIVE)) {
    listeners_to_remove.insert(listener.get().name());
  }
  for (const auto& resource : resources) {
    // Remove its name from our delta removed pile.
    listeners_to_remove.erase(resource.get().name());
  }
  // Copy our delta removed pile into the desired format.
  Protobuf::RepeatedPtrField<std::string> to_remove_repeated;
  for (const auto& listener : listeners_to_remove) {
    *to_remove_repeated.Add() = listener;
  }
  return onConfigUpdate(resources, to_remove_repeated, version_info);
}

void LdsApiImpl::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                      const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad
  // config.
  init_target_.ready();
}

} // namespace Server
} // namespace Envoy
