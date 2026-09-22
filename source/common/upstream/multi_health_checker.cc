#include "source/common/upstream/multi_health_checker.h"

#include "source/common/upstream/health_checker_impl.h"

namespace Envoy {
namespace Upstream {

namespace {

constexpr uint32_t kActiveHcFlagMask = static_cast<uint32_t>(Host::HealthFlag::FAILED_ACTIVE_HC) |
                                       static_cast<uint32_t>(Host::HealthFlag::DEGRADED_ACTIVE_HC) |
                                       static_cast<uint32_t>(Host::HealthFlag::PENDING_ACTIVE_HC) |
                                       static_cast<uint32_t>(Host::HealthFlag::ACTIVE_HC_TIMEOUT);

} // namespace

MultiHealthChecker::MultiHealthChecker(Cluster& cluster)
    : cluster_(cluster), stat_name_pool_(cluster.info()->statsScope().symbolTable()),
      healthy_gauge_(cluster.info()->statsScope().gaugeFromStatName(
          stat_name_pool_.add("health_check.healthy"), Stats::Gauge::ImportMode::Accumulate)),
      degraded_gauge_(cluster.info()->statsScope().gaugeFromStatName(
          stat_name_pool_.add("health_check.degraded"), Stats::Gauge::ImportMode::Accumulate)) {}

absl::StatusOr<std::shared_ptr<MultiHealthChecker>> MultiHealthChecker::create(
    Cluster& cluster,
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck>& health_checks,
    Server::Configuration::ServerFactoryContext& server_context) {

  auto checker = std::shared_ptr<MultiHealthChecker>(new MultiHealthChecker(cluster));

  checker->checkers_.reserve(health_checks.size());
  for (int i = 0; i < health_checks.size(); i++) {
    const auto& sub_config = health_checks[i];

    if (sub_config.name().empty()) {
      return absl::InvalidArgumentError(
          fmt::format("health check at index {} is missing a name; all health checks "
                      "must have a name when multiple health checks are configured",
                      i));
    }

    const uint32_t checker_idx = static_cast<uint32_t>(i);

    checker->checkers_.emplace_back(*checker, checker_idx);
    auto& data = checker->checkers_.back();

    auto checker_or_error =
        HealthCheckerFactory::create(sub_config, cluster, server_context, data.flag_callbacks);
    RETURN_IF_NOT_OK(checker_or_error.status());

    data.checker = std::move(checker_or_error.value());

    data.checker->addHostCheckCompleteCb(
        [raw = checker.get(), checker_idx](const HostSharedPtr& host,
                                           HealthTransition changed_state, HealthState result) {
          raw->onCheckerResult(checker_idx, host, changed_state, result);
        });
  }

  checker->member_update_cb_ = cluster.prioritySet().addMemberUpdateCb(
      [raw = checker.get()](const HostVector& hosts_added, const HostVector& hosts_removed) {
        raw->onClusterMemberUpdate(hosts_added, hosts_removed);
      });

  return checker;
}

MultiHealthChecker::~MultiHealthChecker() {
  for (const auto& [_, state] : host_states_) {
    adjustGauges(state, &Stats::Gauge::dec);
  }
}

bool MultiHealthChecker::SubCheckerHealthFlagCallbacks::get(const Host& host,
                                                            Host::HealthFlag flag) const {
  auto it = parent_.checkers_[checker_idx_].host_flags.find(&host);
  if (it == parent_.checkers_[checker_idx_].host_flags.end()) {
    return false;
  }
  return (it->second & static_cast<uint32_t>(flag)) != 0;
}

void MultiHealthChecker::SubCheckerHealthFlagCallbacks::set(Host& host, Host::HealthFlag flag) {
  parent_.checkers_[checker_idx_].host_flags[&host] |= static_cast<uint32_t>(flag);
}

void MultiHealthChecker::SubCheckerHealthFlagCallbacks::clear(Host& host, Host::HealthFlag flag) {
  parent_.checkers_[checker_idx_].host_flags[&host] &= ~static_cast<uint32_t>(flag);
}

void MultiHealthChecker::adjustGauges(const PerHostState& state, void (Stats::Gauge::*op)()) {
  if (isGaugeHealthy(state)) {
    (healthy_gauge_.*op)();
  }
  if (isGaugeDegraded(state)) {
    (degraded_gauge_.*op)();
  }
}

void MultiHealthChecker::addHostCheckCompleteCb(HostStatusCb callback) {
  callbacks_.push_back(std::move(callback));
}

void MultiHealthChecker::start() {
  for (const auto& host_set : cluster_.prioritySet().hostSetsPerPriority()) {
    for (const auto& host : host_set->hosts()) {
      initializeHost(host);
    }
  }

  for (auto& data : checkers_) {
    data.checker->start();
  }
}

void MultiHealthChecker::initializeHost(const HostSharedPtr& host) {
  uint32_t initial_flags = host->healthFlagsGetAll() & kActiveHcFlagMask;
  for (auto& data : checkers_) {
    data.host_flags[host.get()] = initial_flags;
  }

  ASSERT(checkers_.size() > 0 && checkers_.size() <= 32, "32 bit shifts are UB");
  const uint32_t all_bits = ~uint32_t{0} >> (32 - checkers_.size());
  auto& state = host_states_[host.get()];
  state.pending_bits = host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC) ? all_bits : 0;
  state.fail_bits = host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC) ? all_bits : 0;
  state.degraded_bits = host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC) ? all_bits : 0;
  state.initial_check_pending = all_bits;

  adjustGauges(host_states_[host.get()], &Stats::Gauge::inc);
}

void MultiHealthChecker::onClusterMemberUpdate(const HostVector& hosts_added,
                                               const HostVector& hosts_removed) {
  for (const auto& host : hosts_added) {
    initializeHost(host);
  }
  for (const auto& host : hosts_removed) {
    auto state_it = host_states_.find(host.get());
    ASSERT(state_it != host_states_.end());
    if (state_it != host_states_.end()) {
      adjustGauges(state_it->second, &Stats::Gauge::dec);
      host_states_.erase(state_it);
    }
    for (auto& data : checkers_) {
      data.host_flags.erase(host.get());
    }
  }
}

void MultiHealthChecker::onCheckerResult(uint32_t checker_index, HostSharedPtr host,
                                         HealthTransition /*changed_state*/,
                                         HealthState /*result*/) {
  const uint32_t bit = 1u << checker_index;

  auto state_it = host_states_.find(host.get());
  ASSERT(state_it != host_states_.end());
  auto& state = state_it->second;

  uint32_t checker_flags = checkers_[checker_index].host_flags[host.get()];

  if (checker_flags & static_cast<uint32_t>(Host::HealthFlag::FAILED_ACTIVE_HC)) {
    state.fail_bits |= bit;
  } else {
    state.fail_bits &= ~bit;
  }

  if (checker_flags & static_cast<uint32_t>(Host::HealthFlag::DEGRADED_ACTIVE_HC)) {
    state.degraded_bits |= bit;
  } else {
    state.degraded_bits &= ~bit;
  }

  if (checker_flags & static_cast<uint32_t>(Host::HealthFlag::PENDING_ACTIVE_HC)) {
    state.pending_bits |= bit;
  } else {
    state.pending_bits &= ~bit;
  }

  // Don't do any operations with a side effect until all checkers have posted their initial result.
  state.initial_check_pending &= ~bit;
  if (state.initial_check_pending != 0) {
    return;
  }

  // Derive "was" from host flags, which reflect the last-applied state. This is correct both
  // during normal operation and when the gate above just lifted, since no side effects run
  // during the gated period.
  const bool was_aggregate_failed = host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC);
  const bool was_aggregate_degraded = host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  const bool was_aggregate_pending = host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC);

  const bool now_aggregate_failed = state.fail_bits != 0;
  const bool now_aggregate_degraded = state.degraded_bits != 0;
  const bool now_aggregate_pending = state.pending_bits != 0;

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

  if (was_aggregate_failed != now_aggregate_failed) {
    if (now_aggregate_failed) {
      healthy_gauge_.dec();
    } else {
      healthy_gauge_.inc();
    }
  }
  if (was_aggregate_degraded != now_aggregate_degraded) {
    if (now_aggregate_degraded) {
      degraded_gauge_.inc();
    } else {
      degraded_gauge_.dec();
    }
  }

  if (was_aggregate_pending && now_aggregate_pending) {
    return;
  }

  HealthTransition aggregate_transition = HealthTransition::Unchanged;
  if (was_aggregate_failed != now_aggregate_failed ||
      was_aggregate_degraded != now_aggregate_degraded ||
      was_aggregate_pending != now_aggregate_pending) {
    aggregate_transition = HealthTransition::Changed;
  }

  HealthState aggregate_result =
      now_aggregate_failed ? HealthState::Unhealthy : HealthState::Healthy;

  for (const auto& cb : callbacks_) {
    cb(host, aggregate_transition, aggregate_result);
  }
}

} // namespace Upstream
} // namespace Envoy
