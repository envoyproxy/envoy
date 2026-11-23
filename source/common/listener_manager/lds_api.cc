#include "source/common/listener_manager/lds_api.h"

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/listener/v3/listener.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/common/cleanup.h"
#include "source/common/config/api_version.h"
#include "source/common/config/opaque_resource_decoder_impl.h"
#include "source/common/config/resource_name.h"
#include "source/common/config/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/node_hash_set.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Server {
namespace {

// Generates a UUID name for a listener without one, as the listener manager would, so that a SotW
// update can remove the listener by that name.
class ListenerResourceDecoder
    : public Config::OpaqueResourceDecoderImpl<envoy::config::listener::v3::Listener> {
public:
  ListenerResourceDecoder(ProtobufMessage::ValidationVisitor& validation_visitor,
                          Random::RandomGenerator& random)
      : OpaqueResourceDecoderImpl(validation_visitor, "name"), random_(random) {}

  // Config::OpaqueResourceDecoder
  Protobuf::Arena::UniquePtr<Protobuf::Message> decodeResource(const Protobuf::Any& resource,
                                                               Protobuf::Arena& arena) override {
    Protobuf::Arena::UniquePtr<Protobuf::Message> message =
        OpaqueResourceDecoderImpl::decodeResource(resource, arena);
    auto& listener = Protobuf::DynamicCastMessage<envoy::config::listener::v3::Listener>(*message);
    if (listener.name().empty()) {
      listener.set_name(random_.uuid());
    }
    return message;
  }

private:
  Random::RandomGenerator& random_;
};

} // namespace

LdsApiImpl::LdsApiImpl(const envoy::config::core::v3::ConfigSource& lds_config,
                       const xds::core::v3::ResourceLocator* lds_resources_locator,
                       Upstream::ClusterManager& cm, Init::Manager& init_manager,
                       Stats::Scope& scope, ListenerManager& lm,
                       ProtobufMessage::ValidationVisitor& validation_visitor,
                       Random::RandomGenerator& random)
    : listener_manager_(lm), scope_(scope.createScope("listener_manager.lds.")),
      resource_decoder_(std::make_shared<ListenerResourceDecoder>(validation_visitor, random)),
      init_target_("LDS", [this]() { subscription_->start({}); }) {
  const auto resource_name = Config::getResourceName<envoy::config::listener::v3::Listener>();
  if (lds_resources_locator == nullptr) {
    subscription_ = THROW_OR_RETURN_VALUE(cm.subscriptionFactory().subscriptionFromConfigSource(
                                              lds_config, Grpc::Common::typeUrl(resource_name),
                                              *scope_, *this, resource_decoder_, {}),
                                          Config::SubscriptionPtr);
  } else {
    subscription_ = THROW_OR_RETURN_VALUE(
        cm.subscriptionFactory().collectionSubscriptionFromUrl(
            *lds_resources_locator, lds_config, resource_name, *scope_, *this, resource_decoder_),
        Config::SubscriptionPtr);
  }
  init_manager.add(init_target_);
}

absl::Status
LdsApiImpl::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                           const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                           const std::string& system_version_info) {
  bool any_applied = false;
  listener_manager_.beginListenerUpdate();

  // We do all listener removals before adding the new listeners. This allows adding a new
  // listener with the same address as a listener that is to be removed. Do not change the order.
  for (const auto& removed_listener : removed_resources) {
    // SotW listeners whose TTL expires are removed here, so they are no longer tracked.
    sotw_listener_names_.erase(removed_listener);
    if (listener_manager_.removeListener(removed_listener)) {
      ENVOY_LOG(info, "lds: remove listener '{}'", removed_listener);
      any_applied = true;
    }
  }

  ListenerManager::FailureStates failure_state;
  absl::node_hash_set<std::string> listener_names;
  std::string message;

  for (const auto& resource : added_resources) {
    // Holds a reference to the name of the currently parsed listener resource.
    // This is needed for the onError clause below.
    absl::string_view listener_name = EMPTY_STRING;

    auto onError = [&](std::string error_message) {
      failure_state.emplace_back(envoy::admin::v3::UpdateFailureState());
      auto& state = failure_state.back();
      state.set_details(error_message);
#if defined(ENVOY_ENABLE_FULL_PROTOS)
      std::ignore = state.mutable_failed_configuration()->PackFrom(resource.get().resource());
#endif
      absl::StrAppend(&message, listener_name, ": ", error_message, "\n");
      ENVOY_LOG(warn, "lds: listener '{}' config rejected: {}", listener_name, error_message);
    };

    TRY_ASSERT_MAIN_THREAD {
      const envoy::config::listener::v3::Listener& listener =
          Envoy::Protobuf::DynamicCastMessage<envoy::config::listener::v3::Listener>(
              resource.get().resource());
      listener_name = listener.name();
      if (!listener_names.insert(listener.name()).second) {
        // NOTE: at this point, the first of these duplicates has already been successfully
        // applied.
        onError(fmt::format("duplicate listener {} found", listener_name));
        continue;
      }
      absl::StatusOr<bool> update_or_error =
          listener_manager_.addOrUpdateListener(listener, resource.get().version(), true);
      if (!update_or_error.status().ok()) {
        onError(std::string(update_or_error.status().message()));
        continue;
      }
      if (update_or_error.value()) {
        ENVOY_LOG(info, "lds: add/update listener '{}'", listener_name);
        any_applied = true;
      } else {
        ENVOY_LOG(debug, "lds: add/update listener '{}' skipped", listener_name);
      }
    }
    END_TRY
    CATCH(EnvoyException & e, { onError(e.what()); })
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
  absl::flat_hash_set<std::string> next_sotw_listener_names;
  next_sotw_listener_names.reserve(resources.size());
  for (const auto& resource : resources) {
    next_sotw_listener_names.insert(
        Protobuf::DynamicCastMessage<envoy::config::listener::v3::Listener>(
            resource.get().resource())
            .name());
  }

  // Other components may add listeners, so only remove the listeners received via SotW.
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.lds_sotw_track_resources")) {
    Protobuf::RepeatedPtrField<std::string> to_remove;
    for (const std::string& listener_name : sotw_listener_names_) {
      if (!next_sotw_listener_names.contains(listener_name)) {
        to_remove.Add(std::string(listener_name));
      }
    }
    absl::Status status = onConfigUpdate(resources, to_remove, version_info);
    sotw_listener_names_ = std::move(next_sotw_listener_names);
    return status;
  }

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
  absl::Status status = onConfigUpdate(resources, to_remove_repeated, version_info);
  // Track the names even if unused, so enabling the guard at runtime doesn't leak listeners. Store
  // them after the call, since it untracks the listeners that it removes.
  sotw_listener_names_ = std::move(next_sotw_listener_names);
  return status;
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
