#include "source/extensions/config_subscription/grpc/delta_subscription_state.h"

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Config {
namespace {

DeltaSubscriptionStateVariant getState(std::string type_url,
                                       UntypedConfigUpdateCallbacks& watch_map,
                                       const LocalInfo::LocalInfo& local_info,
                                       Event::Dispatcher& dispatcher,
                                       XdsConfigTrackerOptRef xds_config_tracker) {
  if (Runtime::runtimeFeatureEnabled("envoy.restart_features.explicit_wildcard_resource")) {
    return DeltaSubscriptionStateVariant(absl::in_place_type<NewDeltaSubscriptionState>,
                                         std::move(type_url), watch_map, local_info, dispatcher,
                                         xds_config_tracker);
  } else {
    return DeltaSubscriptionStateVariant(absl::in_place_type<OldDeltaSubscriptionState>,
                                         std::move(type_url), watch_map, local_info, dispatcher,
                                         xds_config_tracker);
  }
}

} // namespace

DeltaSubscriptionState::DeltaSubscriptionState(std::string type_url,
                                               UntypedConfigUpdateCallbacks& watch_map,
                                               const LocalInfo::LocalInfo& local_info,
                                               Event::Dispatcher& dispatcher,
                                               XdsConfigTrackerOptRef xds_config_tracker)
    : state_(getState(std::move(type_url), watch_map, local_info, dispatcher, xds_config_tracker)) {
}

void DeltaSubscriptionState::updateSubscriptionInterest(
    const absl::flat_hash_set<std::string>& cur_added,
    const absl::flat_hash_set<std::string>& cur_removed) {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(&state_); state != nullptr) {
    state->updateSubscriptionInterest(cur_added, cur_removed);
    return;
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  state.updateSubscriptionInterest(cur_added, cur_removed);
}

void DeltaSubscriptionState::setMustSendDiscoveryRequest() {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(&state_); state != nullptr) {
    state->setMustSendDiscoveryRequest();
    return;
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  state.setMustSendDiscoveryRequest();
}

bool DeltaSubscriptionState::subscriptionUpdatePending() const {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(&state_); state != nullptr) {
    return state->subscriptionUpdatePending();
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  return state.subscriptionUpdatePending();
}

void DeltaSubscriptionState::markStreamFresh() {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(&state_); state != nullptr) {
    state->markStreamFresh();
    return;
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  state.markStreamFresh();
}

UpdateAck DeltaSubscriptionState::handleResponse(
    const envoy::service::discovery::v3::DeltaDiscoveryResponse& message) {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(&state_); state != nullptr) {
    return state->handleResponse(message);
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  return state.handleResponse(message);
}

void DeltaSubscriptionState::handleEstablishmentFailure() {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(&state_); state != nullptr) {
    state->handleEstablishmentFailure();
    return;
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  state.handleEstablishmentFailure();
}

envoy::service::discovery::v3::DeltaDiscoveryRequest
DeltaSubscriptionState::getNextRequestAckless() {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(&state_); state != nullptr) {
    return state->getNextRequestAckless();
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  return state.getNextRequestAckless();
}

envoy::service::discovery::v3::DeltaDiscoveryRequest
DeltaSubscriptionState::getNextRequestWithAck(const UpdateAck& ack) {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(&state_); state != nullptr) {
    return state->getNextRequestWithAck(ack);
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  return state.getNextRequestWithAck(ack);
}

} // namespace Config
} // namespace Envoy
