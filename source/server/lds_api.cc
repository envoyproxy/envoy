#include "server/lds_api.h"

#include <unordered_map>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/api/v2/listener.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/config/listener/v3/listener.pb.validate.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/cleanup.h"
#include "common/config/api_version.h"
#include "common/config/resources.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Server {

LdsApiImpl::LdsApiImpl(const envoy::config::core::v3::ConfigSource& lds_config,
                       Upstream::ClusterManager& cm, Init::Manager& init_manager,
                       Stats::Scope& scope, ListenerManager& lm,
                       ProtobufMessage::ValidationVisitor& validation_visitor)
    : listener_manager_(lm), scope_(scope.createScope("listener_manager.lds.")), cm_(cm),
      init_target_("LDS", [this]() { subscription_->start({}); }),
      validation_visitor_(validation_visitor) {
  const auto resource_name = getResourceName(lds_config.resource_api_version());
  subscription_ = cm.subscriptionFactory().subscriptionFromConfigSource(
      lds_config, Grpc::Common::typeUrl(resource_name), *scope_, *this);
  init_manager.add(init_target_);
}

void LdsApiImpl::onConfigUpdate(
    const Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources,
    const std::string& system_version_info) {
  std::unique_ptr<Cleanup> maybe_eds_resume;
  if (cm_.adsMux()) {
    cm_.adsMux()->pause(Config::TypeUrl::get().RouteConfiguration);
    maybe_eds_resume = std::make_unique<Cleanup>(
        [this] { cm_.adsMux()->resume(Config::TypeUrl::get().RouteConfiguration); });
  }

  bool any_applied = false;
  listener_manager_.beginListenerUpdate();

  // We do all listener removals before adding the new listeners. This allows adding a new listener
  // with the same address as a listener that is to be removed. Do not change the order.
  for (const auto& removed_listener : removed_resources) {
    if (listener_manager_.removeListener(removed_listener)) {
      ENVOY_LOG(info, "lds: remove listener '{}'", removed_listener);
      any_applied = true;
    }
  }

  ListenerManager::FailureStates failure_state;
  std::unordered_set<std::string> listener_names;
  std::string message;
  for (const auto& resource : added_resources) {
    envoy::config::listener::v3::Listener listener;
    try {
      listener = MessageUtil::anyConvertAndValidate<envoy::config::listener::v3::Listener>(
          resource.resource(), validation_visitor_);
      if (!listener_names.insert(listener.name()).second) {
        // NOTE: at this point, the first of these duplicates has already been successfully applied.
        throw EnvoyException(fmt::format("duplicate listener {} found", listener.name()));
      }
      if (listener_manager_.addOrUpdateListener(listener, resource.version(), true)) {
        ENVOY_LOG(info, "lds: add/update listener '{}'", listener.name());
        any_applied = true;
      } else {
        ENVOY_LOG(debug, "lds: add/update listener '{}' skipped", listener.name());
      }
    } catch (const EnvoyException& e) {
      failure_state.push_back(std::make_unique<envoy::admin::v3::UpdateFailureState>());
      auto& state = failure_state.back();
      state->set_details(e.what());
      state->mutable_failed_configuration()->PackFrom(resource);
      absl::StrAppend(&message, listener.name(), ": ", e.what(), "\n");
    }
  }
  listener_manager_.endListenerUpdate(std::move(failure_state));

  if (any_applied) {
    system_version_info_ = system_version_info;
  }
  init_target_.ready();
  if (!message.empty()) {
    throw EnvoyException(fmt::format("Error adding/updating listener(s) {}", message));
  }
}

void LdsApiImpl::onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                                const std::string& version_info) {
  // We need to keep track of which listeners need to remove.
  // Specifically, it's [listeners we currently have] - [listeners found in the response].
  std::unordered_set<std::string> listeners_to_remove;
  for (const auto& listener : listener_manager_.listeners()) {
    listeners_to_remove.insert(listener.get().name());
  }

  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> to_add_repeated;
  for (const auto& listener_blob : resources) {
    // Add this resource to our delta added/updated pile...
    envoy::service::discovery::v3::Resource* to_add = to_add_repeated.Add();
    // No validation needed here the overloaded call to onConfigUpdate validates.
    const std::string listener_name =
        MessageUtil::anyConvert<envoy::config::listener::v3::Listener>(listener_blob).name();
    to_add->set_name(listener_name);
    to_add->set_version(version_info);
    to_add->mutable_resource()->MergeFrom(listener_blob);
    // ...and remove its name from our delta removed pile.
    listeners_to_remove.erase(listener_name);
  }

  // Copy our delta removed pile into the desired format.
  Protobuf::RepeatedPtrField<std::string> to_remove_repeated;
  for (const auto& listener : listeners_to_remove) {
    *to_remove_repeated.Add() = listener;
  }
  onConfigUpdate(to_add_repeated, to_remove_repeated, version_info);
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
