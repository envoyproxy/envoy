#include "source/common/upstream/health_checker_group.h"

namespace Envoy {
namespace Upstream {

// HealthCheckHostProxy implementation

void HealthCheckHostProxy::healthFlagSet(HealthFlag flag) {
  if (isAggregatedFlag(flag)) {
    local_flags_ |= static_cast<uint32_t>(flag);
  } else {
    host_.healthFlagSet(flag);
  }
}

bool HealthCheckHostProxy::healthFlagGet(HealthFlag flag) const {
  if (isAggregatedFlag(flag)) {
    return (local_flags_ & static_cast<uint32_t>(flag)) != 0;
  }
  return host_.healthFlagGet(flag);
}

void HealthCheckHostProxy::healthFlagClear(HealthFlag flag) {
  if (isAggregatedFlag(flag)) {
    local_flags_ &= ~static_cast<uint32_t>(flag);
  } else {
    host_.healthFlagClear(flag);
  }
}

// HealthCheckerGroup implementation

void HealthCheckerGroup::addChecker(HealthCheckerSharedPtr checker,
                                    const std::string& stat_prefix) {
  const uint32_t index = checkers_.size();
  checkers_.push_back(checker);
  stat_prefixes_.push_back(stat_prefix);
  host_proxies_.emplace_back();

  checker->addHostCheckCompleteCb(
      [this, index](const HostSharedPtr& host, HealthTransition changed_state, HealthState result) {
        onCheckerResult(index, host, changed_state, result);
      });
}

void HealthCheckerGroup::addHostCheckCompleteCb(HostStatusCb callback) {
  callbacks_.push_back(callback);
}

void HealthCheckerGroup::start() {
  for (auto& checker : checkers_) {
    checker->start();
  }
}

HostHealth& HealthCheckerGroup::getOrCreateHostHealth(uint32_t checker_index, Host& host) {
  auto& proxy_map = host_proxies_[checker_index];
  auto it = proxy_map.find(&host);
  if (it != proxy_map.end()) {
    return *it->second;
  }

  // Initialize the proxy with the host's current flags for the aggregated flags.
  // New hosts start with FAILED_ACTIVE_HC and PENDING_ACTIVE_HC set.
  uint32_t initial_flags = 0;
  if (host.healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    initial_flags |= static_cast<uint32_t>(Host::HealthFlag::FAILED_ACTIVE_HC);
  }
  if (host.healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC)) {
    initial_flags |= static_cast<uint32_t>(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  }
  if (host.healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC)) {
    initial_flags |= static_cast<uint32_t>(Host::HealthFlag::PENDING_ACTIVE_HC);
  }

  auto proxy = std::make_unique<HealthCheckHostProxy>(host, initial_flags);
  auto& ref = *proxy;
  proxy_map[&host] = std::move(proxy);
  return ref;
}

void HealthCheckerGroup::onCheckerResult(uint32_t checker_index, const HostSharedPtr& host,
                                         HealthTransition changed_state, HealthState result) {
  const uint32_t bit = 1u << checker_index;
  auto [it, inserted] = host_states_.try_emplace(host, PerHostState{});
  if (inserted) {
    // First time seeing this host. Initialize all checker bits as failed and pending,
    // matching the initial host state (hosts start with FAILED_ACTIVE_HC and PENDING_ACTIVE_HC).
    const uint32_t all_bits = (1u << checkers_.size()) - 1;
    it->second.fail_bits = all_bits;
    it->second.pending_bits = all_bits;
  }
  auto& state = it->second;

  // Track the old aggregate state.
  const bool was_aggregate_failed = state.fail_bits != 0;
  const bool was_aggregate_degraded = state.degraded_bits != 0;
  const bool was_aggregate_pending = state.pending_bits != 0;

  // Update per-checker fail bits based on the callback parameters.
  // This works regardless of whether the inner checker uses a proxy or not.
  if (changed_state == HealthTransition::Changed) {
    if (result == HealthState::Unhealthy) {
      state.fail_bits |= bit;
    } else {
      state.fail_bits &= ~bit;
    }
  }

  // Track degraded state. If a proxy exists for this checker/host, read from it
  // (the inner checker wrote to the proxy's local flags). Otherwise, read from
  // the host flag directly (non-proxy-backed checkers like mocks write to the host).
  auto& proxy_map = host_proxies_[checker_index];
  auto proxy_it = proxy_map.find(host.get());
  if (proxy_it != proxy_map.end()) {
    if (proxy_it->second->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC)) {
      state.degraded_bits |= bit;
    } else {
      state.degraded_bits &= ~bit;
    }
  } else {
    if (host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC)) {
      state.degraded_bits |= bit;
    } else {
      state.degraded_bits &= ~bit;
    }
  }

  // Track pending: on any callback from a checker, that checker is no longer pending.
  state.pending_bits &= ~bit;

  // Compute new aggregate state.
  const bool now_aggregate_failed = state.fail_bits != 0;
  const bool now_aggregate_degraded = state.degraded_bits != 0;
  const bool now_aggregate_pending = state.pending_bits != 0;

  // Correct the host flags to reflect the aggregate.
  if (now_aggregate_failed) {
    host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  } else {
    host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  }

  if (now_aggregate_degraded) {
    host->healthFlagSet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  } else {
    host->healthFlagClear(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  }

  if (now_aggregate_pending) {
    host->healthFlagSet(Host::HealthFlag::PENDING_ACTIVE_HC);
  } else {
    host->healthFlagClear(Host::HealthFlag::PENDING_ACTIVE_HC);
  }

  // Suppress outer callbacks until all checkers have responded at least once for this host.
  // This ensures the cluster's pending_initialize_health_checks_ counter (which counts one per
  // host) sees exactly one callback per host during initialization.
  if (was_aggregate_pending && now_aggregate_pending) {
    // Still waiting for more checkers to respond. Correct the flags but don't notify.
    return;
  }

  // Determine the aggregate transition.
  HealthTransition aggregate_transition = HealthTransition::Unchanged;
  if (was_aggregate_failed != now_aggregate_failed ||
      was_aggregate_degraded != now_aggregate_degraded ||
      was_aggregate_pending != now_aggregate_pending) {
    aggregate_transition = HealthTransition::Changed;
  } else if (changed_state == HealthTransition::ChangePending) {
    aggregate_transition = HealthTransition::ChangePending;
  }

  // Determine aggregate health state for the callback.
  HealthState aggregate_result =
      now_aggregate_failed ? HealthState::Unhealthy : HealthState::Healthy;

  // Fire outer callbacks.
  for (const auto& cb : callbacks_) {
    cb(host, aggregate_transition, aggregate_result);
  }
}

std::vector<HealthChecker::PerCheckerStatus>
HealthCheckerGroup::perCheckerStatus(const Host& host) const {
  std::vector<PerCheckerStatus> result;
  result.reserve(checkers_.size());
  for (uint32_t i = 0; i < checkers_.size(); i++) {
    PerCheckerStatus info;
    info.stat_prefix = stat_prefixes_[i];
    // Look up the proxy for this checker/host to read per-checker flags.
    const auto& proxy_map = host_proxies_[i];
    auto it = proxy_map.find(const_cast<Host*>(&host));
    if (it != proxy_map.end()) {
      info.failed = it->second->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC);
      info.degraded = it->second->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
      info.pending = it->second->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC);
    } else {
      // No proxy yet (host hasn't been checked), assume initial state.
      info.failed = true;
      info.pending = true;
    }
    result.push_back(std::move(info));
  }
  return result;
}

} // namespace Upstream
} // namespace Envoy
