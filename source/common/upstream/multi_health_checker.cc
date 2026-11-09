#include "source/common/upstream/multi_health_checker.h"

#include <limits>

#include "source/common/common/enum_to_int.h"
#include "source/common/upstream/health_checker_impl.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Upstream {

namespace {

constexpr uint32_t kActiveHcFlagMask = enumToInt(Host::HealthFlag::FAILED_ACTIVE_HC) |
                                       enumToInt(Host::HealthFlag::DEGRADED_ACTIVE_HC) |
                                       enumToInt(Host::HealthFlag::PENDING_ACTIVE_HC) |
                                       enumToInt(Host::HealthFlag::ACTIVE_HC_TIMEOUT) |
                                       enumToInt(Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL);

} // namespace

MultiHealthChecker::PerHostState::PerHostState(uint8_t num_checkers, const Host& host) {
  ASSERT(num_checkers > 0 && num_checkers <= kMaxHealthChecks,
         "bit shifts larger than size are UB");

  static_assert(kActiveHcFlagMask <=
                    std::numeric_limits<decltype(checker_flags_)::value_type>::max(),
                "If this fails, checker_flags_ probably needs a larger type");

  const uint8_t all_bits = uint8_t{0xff} >> (kMaxHealthChecks - num_checkers);

  const uint32_t host_flags_all = host.healthFlagsGetAll();
  checker_flags_.fill(host_flags_all & kActiveHcFlagMask);

  auto flagBits = [&](Host::HealthFlag flag) -> uint8_t {
    return (host_flags_all & enumToInt(flag)) ? all_bits : 0;
  };
  fail_bits_ = flagBits(Host::HealthFlag::FAILED_ACTIVE_HC);
  degraded_bits_ = flagBits(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  pending_bits_ = flagBits(Host::HealthFlag::PENDING_ACTIVE_HC);
  timeout_bits_ = flagBits(Host::HealthFlag::ACTIVE_HC_TIMEOUT);
  immediate_fail_bits_ = flagBits(Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL);

  initial_check_pending_bits_ = all_bits;
}

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

  ASSERT(health_checks.size() <= kMaxHealthChecks);

  auto checker = std::shared_ptr<MultiHealthChecker>(new MultiHealthChecker(cluster));

  checker->checkers_.reserve(health_checks.size());
  absl::flat_hash_set<absl::string_view> seen_names;
  for (uint8_t checker_idx = 0; checker_idx < static_cast<uint8_t>(health_checks.size());
       checker_idx++) {
    const auto& sub_config = health_checks[checker_idx];

    if (sub_config.name().empty()) {
      return absl::InvalidArgumentError(
          fmt::format("health check at index {} is missing a name; all health checks "
                      "must have a name when multiple health checks are configured",
                      checker_idx));
    }

    if (!seen_names.insert(sub_config.name()).second) {
      return absl::InvalidArgumentError(
          fmt::format("duplicate health check name '{}' at index {}; all health check "
                      "names must be unique within a cluster",
                      sub_config.name(), checker_idx));
    }

    PerCheckerData& data = checker->checkers_.emplace_back(*checker, checker_idx);

    auto checker_or_error =
        HealthCheckerFactory::create(sub_config, cluster, server_context, data.flag_callbacks);
    RETURN_IF_NOT_OK(checker_or_error.status());

    data.checker = std::move(checker_or_error.value());

    data.checker->addHostCheckCompleteCb(
        [multi_checker = checker.get(), checker_idx](
            const HostSharedPtr& host, HealthTransition changed_state, HealthState result) {
          multi_checker->onCheckerResult(checker_idx, host, changed_state, result);
        });
  }

  // Register after creating sub-checkers: each sub-checker's constructor also registers a
  // member update callback, and our callback must fire last so that host_states_ entries
  // remain accessible during sub-checker session teardown on host removal.
  checker->member_update_cb_ = cluster.prioritySet().addMemberUpdateCb(
      [multi_checker = checker.get()](const HostVector& hosts_added,
                                      const HostVector& hosts_removed) {
        multi_checker->onClusterMemberUpdate(hosts_added, hosts_removed);
      });

  return checker;
}

MultiHealthChecker::~MultiHealthChecker() {
  for (const auto& [_, state] : host_states_) {
    adjustGauges(state, &Stats::Gauge::dec);
  }
}

bool MultiHealthChecker::SubCheckerHealthFlagCallbacks::get(const Host& host,
                                                            Host::HealthFlag flag) {
  return (hostFlags(host) & enumToInt(flag)) != 0;
}

void MultiHealthChecker::SubCheckerHealthFlagCallbacks::set(Host& host, Host::HealthFlag flag) {
  hostFlags(host) |= enumToInt(flag);
}

void MultiHealthChecker::SubCheckerHealthFlagCallbacks::clear(Host& host, Host::HealthFlag flag) {
  hostFlags(host) &= ~enumToInt(flag);
}

uint16_t& MultiHealthChecker::SubCheckerHealthFlagCallbacks::hostFlags(const Host& host) {
  ASSERT(parent_.getOrCreateHostState(host).checker_flags_.size() > checker_idx_);
  return parent_.getOrCreateHostState(host).checker_flags_[checker_idx_];
}

void MultiHealthChecker::adjustGauges(const PerHostState& state, void (Stats::Gauge::*op)()) {
  if (state.initial_check_pending_bits_ != 0) {
    return;
  }

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
      getOrCreateHostState(*host);
    }
  }

  for (auto& data : checkers_) {
    data.checker->start();
  }

  started_ = true;
}

MultiHealthChecker::PerHostState& MultiHealthChecker::getOrCreateHostState(const Host& host) {
  auto [it, inserted] = host_states_.try_emplace(&host, checkers_.size(), host);
  return it->second;
}

void MultiHealthChecker::onClusterMemberUpdate(const HostVector& hosts_added,
                                               const HostVector& hosts_removed) {
  if (!started_) {
    return;
  }

  for (const auto& host : hosts_added) {
    // This state is created on-demand so this isn't functionally needed, but creating it here
    // allows the size check assertion below to always succeed.
    getOrCreateHostState(*host);
  }

  for (const auto& host : hosts_removed) {
    auto state_it = host_states_.find(host.get());
    ASSERT(state_it != host_states_.end());
    adjustGauges(state_it->second, &Stats::Gauge::dec);
    host_states_.erase(state_it);
  }

  // Verify no stale entries were recreated by a sub-checker callback during this update.
  // This callback fires last because it was registered after the sub-checkers.
  size_t total_hosts = 0;
  for (const auto& host_set : cluster_.prioritySet().hostSetsPerPriority()) {
    total_hosts += host_set->hosts().size();
  }
  ASSERT(host_states_.size() == total_hosts);
}

void MultiHealthChecker::onCheckerResult(uint8_t checker_index, HostSharedPtr host,
                                         HealthTransition /*changed_state*/,
                                         HealthState /*result*/) {
  ASSERT(checker_index < kMaxHealthChecks);
  const uint8_t bit = 1u << checker_index;
  auto& state = getOrCreateHostState(*host);

  ASSERT(checker_index < state.checker_flags_.size(), "Flags must already be initialized");
  uint32_t checker_flags_ = state.checker_flags_[checker_index];

  auto handleBit = [&](uint8_t& bits, Host::HealthFlag flag) {
    if (checker_flags_ & enumToInt(flag)) {
      bits |= bit;
    } else {
      bits &= ~bit;
    }
  };

  handleBit(state.fail_bits_, Host::HealthFlag::FAILED_ACTIVE_HC);
  handleBit(state.degraded_bits_, Host::HealthFlag::DEGRADED_ACTIVE_HC);
  handleBit(state.pending_bits_, Host::HealthFlag::PENDING_ACTIVE_HC);
  handleBit(state.timeout_bits_, Host::HealthFlag::ACTIVE_HC_TIMEOUT);
  handleBit(state.immediate_fail_bits_, Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL);

  const bool gate_was_pending = state.initial_check_pending_bits_ != 0;
  state.initial_check_pending_bits_ &= ~bit;
  if (state.initial_check_pending_bits_ != 0) {
    // Don't do any operations with a side effect until all checkers have posted their initial
    // result.
    return;
  }

  // Capture "was" before updating host flags below.
  const bool was_aggregate_failed = host->healthFlagGet(Host::HealthFlag::FAILED_ACTIVE_HC);
  const bool was_aggregate_degraded = host->healthFlagGet(Host::HealthFlag::DEGRADED_ACTIVE_HC);
  const bool was_aggregate_pending = host->healthFlagGet(Host::HealthFlag::PENDING_ACTIVE_HC);

  const bool now_aggregate_failed = state.fail_bits_ != 0;
  const bool now_aggregate_degraded = state.degraded_bits_ != 0;
  const bool now_aggregate_pending = state.pending_bits_ != 0;
  const bool now_aggregate_timeout = state.timeout_bits_ != 0;
  const bool now_aggregate_excluded = state.immediate_fail_bits_ != 0;

  auto handleFlag = [&](Host::HealthFlag flag, bool value) {
    if (value) {
      host->healthFlagSet(flag);
    } else {
      host->healthFlagClear(flag);
    }
  };

  handleFlag(Host::HealthFlag::FAILED_ACTIVE_HC, now_aggregate_failed);
  handleFlag(Host::HealthFlag::DEGRADED_ACTIVE_HC, now_aggregate_degraded);
  handleFlag(Host::HealthFlag::PENDING_ACTIVE_HC, now_aggregate_pending);
  handleFlag(Host::HealthFlag::ACTIVE_HC_TIMEOUT, now_aggregate_timeout);
  handleFlag(Host::HealthFlag::EXCLUDED_VIA_IMMEDIATE_HC_FAIL, now_aggregate_excluded);

  if (gate_was_pending) {
    // All checkers have now reported their first result. Count this host in gauges based on the
    // aggregate state. Skip transition logic since no prior gauge adjustment has been made.
    adjustGauges(state, &Stats::Gauge::inc);
  } else {
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
