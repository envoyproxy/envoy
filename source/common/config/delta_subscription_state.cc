#include "source/common/config/delta_subscription_state.h"

#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Config {
namespace {

DeltaSubscriptionStateVariant get_state(std::string type_url,
                                        UntypedConfigUpdateCallbacks& watch_map,
                                        const LocalInfo::LocalInfo& local_info,
                                        Event::Dispatcher& dispatcher) {
  if (Runtime::runtimeFeatureEnabled()) {
    return OldDeltaSubscriptionState(std::move(type_url), watch_map, local_info, dispatcher);
  } else {
    return NewDeltaSubscriptionState(std::move(type_url), watch_map, local_info, dispatcher);
  }
}

} // namespace

DeltaSubscriptionState::DeltaSubscriptionState(std::string type_url,
                                               UntypedConfigUpdateCallbacks& watch_map,
                                               const LocalInfo::LocalInfo& local_info,
                                               Event::Dispatcher& dispatcher)
    : state_(get_state(std::move(type_url), watch_map, local_info, dispatcher)) {}

void DeltaSubscriptionState::updateSubscriptionInterest(
    const absl::flat_hash_set<std::string>& cur_added,
    const absl::flat_hash_set<std::string>& cur_removed) {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(state_); state != nullptr) {
    state->updateSubscriptionInterest(cur_added, cur_removed);
    return;
  }
  auto& state = absl::get<OldDeltaSubscriptionState>(state_);
  state.updateSubscriptionInterest(cur_added, cur_removed);
}

void DeltaSubscriptionState::addAliasesToResolve(const absl::flat_hash_set<std::string>& aliases) {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(state_); state != nullptr) {
    state->addAliasesToResolve(aliases);
    return;
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  state.addAliasesToResolve(aliases);
}

void DeltaSubscriptionState::setMustSendDiscoveryRequest() {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(state_); state != nullptr) {
    state->setMustSendDiscoveryRequest();
    return;
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  state.setMustSendDiscoveryRequest();
}

bool DeltaSubscriptionState::subscriptionUpdatePending() const {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(state_); state != nullptr) {
    return state->subscriptionUpdatePending();
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  return state.subscriptionUpdatePending();
}

void DeltaSubscriptionState::markStreamFresh() {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(state_); state != nullptr) {
    state->markStreamFresh();
    return;
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  state.markStreamFresh();
}

UpdateAck DeltaSubscriptionState::handleResponse(
    const envoy::service::discovery::v3::DeltaDiscoveryResponse& message) {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(state_); state != nullptr) {
    return state->handleResponse(message);
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  return state.handleResponse(message);
}

void DeltaSubscriptionState::handleEstablishmentFailure() {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(state_); state != nullptr) {
    state->handleEstablishmentFailure();
    return;
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  state.handleEstablishmentFailure();
}

envoy::service::discovery::v3::DeltaDiscoveryRequest
DeltaSubscriptionState::getNextRequestAckless() {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(state_); state != nullptr) {
    return state->getNextRequestAckless();
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  return state.getNextRequestAckless();
}

envoy::service::discovery::v3::DeltaDiscoveryRequest
DeltaSubscriptionState::getNextRequestWithAck(const UpdateAck& ack) {
  if (auto* state = absl::get_if<OldDeltaSubscriptionState>(state_); state != nullptr) {
    return state->getNextRequestWithAck();
  }
  auto& state = absl::get<NewDeltaSubscriptionState>(state_);
  return state.getNextRequestWithAck();
}

} // namespace Config
} // namespace Envoy
