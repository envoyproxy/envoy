#include "source/extensions/health_checkers/multi/multi.h"

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/upstream/health_checker_impl.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace Multi {

namespace {

// Mask of health flags that are relevant for active health checking.
constexpr uint32_t kActiveHcFlagMask =
    static_cast<uint32_t>(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC) |
    static_cast<uint32_t>(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC) |
    static_cast<uint32_t>(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC) |
    static_cast<uint32_t>(Upstream::Host::HealthFlag::ACTIVE_HC_TIMEOUT);

} // namespace

MultiHealthChecker::MultiHealthChecker(Upstream::Cluster& cluster,
                                       const envoy::config::core::v3::HealthCheck& config,
                                       Server::Configuration::ServerFactoryContext& server_context)
    : cluster_(cluster) {

  const auto& any_config = config.custom_health_check().typed_config();
  envoy::extensions::health_checkers::multi::v3::Multi multi_config;
  THROW_IF_NOT_OK(MessageUtil::unpackTo(any_config, multi_config));

  for (int i = 0; i < multi_config.methods_size(); i++) {
    const auto& method = multi_config.methods(i);

    // Build a synthetic HealthCheck config for this sub-checker by copying the outer config
    // (which has timing, thresholds, transport socket settings) and setting the check method.
    envoy::config::core::v3::HealthCheck sub_config = config;

    switch (method.method_case()) {
    case envoy::extensions::health_checkers::multi::v3::Multi::HealthCheckMethod::kHttpHealthCheck:
      *sub_config.mutable_http_health_check() = method.http_health_check();
      break;
    case envoy::extensions::health_checkers::multi::v3::Multi::HealthCheckMethod::kTcpHealthCheck:
      *sub_config.mutable_tcp_health_check() = method.tcp_health_check();
      break;
    case envoy::extensions::health_checkers::multi::v3::Multi::HealthCheckMethod::kGrpcHealthCheck:
      *sub_config.mutable_grpc_health_check() = method.grpc_health_check();
      break;
    case envoy::extensions::health_checkers::multi::v3::Multi::HealthCheckMethod::
        kCustomHealthCheck:
      *sub_config.mutable_custom_health_check() = method.custom_health_check();
      break;
    default:
      PANIC("unexpected health check method type");
    }

    Stats::ScopeSharedPtr checker_scope;
    Stats::Scope* scope;
    if (!method.name().empty()) {
      std::vector<Stats::TagStringView> tags{{"name", method.name()}};
      checker_scope = cluster.info()->statsScope().createScopeWithTaggedName("health_check", tags,
                                                                             absl::string_view{});
      scope = checker_scope.get();
    } else {
      scope = &cluster.info()->statsScope();
    }

    // Build flag callbacks so this sub-checker operates on local per-host state
    // instead of the real host's flags.
    const uint32_t checker_idx = static_cast<uint32_t>(i);
    Upstream::HealthFlagCallbacks flag_callbacks;
    flag_callbacks.get = [this, checker_idx](const Upstream::Host& host,
                                             Upstream::Host::HealthFlag flag) -> bool {
      auto it = checkers_[checker_idx].host_flags.find(&host);
      if (it == checkers_[checker_idx].host_flags.end()) {
        return false;
      }
      return (it->second & static_cast<uint32_t>(flag)) != 0;
    };
    flag_callbacks.set = [this, checker_idx](Upstream::Host& host,
                                             Upstream::Host::HealthFlag flag) {
      checkers_[checker_idx].host_flags[&host] |= static_cast<uint32_t>(flag);
    };
    flag_callbacks.clear = [this, checker_idx](Upstream::Host& host,
                                               Upstream::Host::HealthFlag flag) {
      checkers_[checker_idx].host_flags[&host] &= ~static_cast<uint32_t>(flag);
    };

    auto checker_or_error = Upstream::HealthCheckerFactory::create(
        sub_config, cluster, server_context, *scope, std::move(flag_callbacks));
    THROW_IF_NOT_OK(checker_or_error.status());

    PerCheckerData data;
    data.checker = std::move(checker_or_error.value());
    data.stat_scope = std::move(checker_scope);

    data.checker->addHostCheckCompleteCb(
        [this, checker_idx](const Upstream::HostSharedPtr& host,
                            Upstream::HealthTransition changed_state,
                            Upstream::HealthState result) {
          onCheckerResult(checker_idx, host, changed_state, result);
        });

    checkers_.push_back(std::move(data));
  }

  member_update_cb_ = cluster_.prioritySet().addMemberUpdateCb(
      [this](const Upstream::HostVector& hosts_added, const Upstream::HostVector& hosts_removed) {
        onClusterMemberUpdate(hosts_added, hosts_removed);
      });
}

void MultiHealthChecker::addHostCheckCompleteCb(HostStatusCb callback) {
  callbacks_.push_back(std::move(callback));
}

void MultiHealthChecker::start() {
  // Initialize host flags for all existing hosts.
  for (const auto& host_set : cluster_.prioritySet().hostSetsPerPriority()) {
    for (const auto& host : host_set->hosts()) {
      initializeHostFlags(host);
    }
  }

  // Start all sub-checkers.
  for (auto& data : checkers_) {
    data.checker->start();
  }
}

void MultiHealthChecker::initializeHostFlags(const Upstream::HostSharedPtr& host) {
  // Initialize per-checker local flags to match what the host currently has for active HC flags.
  uint32_t initial_flags = host->healthFlagsGetAll() & kActiveHcFlagMask;
  for (auto& data : checkers_) {
    data.host_flags[host.get()] = initial_flags;
  }

  // Initialize per-host aggregate state.
  const uint32_t all_bits = (1u << checkers_.size()) - 1;
  auto& state = host_states_[host.get()];
  state.pending_bits =
      host->healthFlagGet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC) ? all_bits : 0;
  state.fail_bits =
      host->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC) ? all_bits : 0;
  state.degraded_bits =
      host->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC) ? all_bits : 0;
}

void MultiHealthChecker::onClusterMemberUpdate(const Upstream::HostVector& hosts_added,
                                               const Upstream::HostVector& hosts_removed) {
  for (const auto& host : hosts_added) {
    initializeHostFlags(host);
  }
  for (const auto& host : hosts_removed) {
    for (auto& data : checkers_) {
      data.host_flags.erase(host.get());
    }
    host_states_.erase(host.get());
  }
}

void MultiHealthChecker::onCheckerResult(uint32_t checker_index, Upstream::HostSharedPtr host,
                                         Upstream::HealthTransition /*changed_state*/,
                                         Upstream::HealthState /*result*/) {
  const uint32_t bit = 1u << checker_index;

  auto state_it = host_states_.find(host.get());
  if (state_it == host_states_.end()) {
    return;
  }
  auto& state = state_it->second;

  const bool was_aggregate_failed = state.fail_bits != 0;
  const bool was_aggregate_degraded = state.degraded_bits != 0;
  const bool was_aggregate_pending = state.pending_bits != 0;

  // Read this checker's local flags to determine its current view of the host.
  uint32_t checker_flags = checkers_[checker_index].host_flags[host.get()];

  if (checker_flags & static_cast<uint32_t>(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC)) {
    state.fail_bits |= bit;
  } else {
    state.fail_bits &= ~bit;
  }

  if (checker_flags & static_cast<uint32_t>(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC)) {
    state.degraded_bits |= bit;
  } else {
    state.degraded_bits &= ~bit;
  }

  if (checker_flags & static_cast<uint32_t>(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC)) {
    state.pending_bits |= bit;
  } else {
    state.pending_bits &= ~bit;
  }

  const bool now_aggregate_failed = state.fail_bits != 0;
  const bool now_aggregate_degraded = state.degraded_bits != 0;
  const bool now_aggregate_pending = state.pending_bits != 0;

  // Update real host flags based on aggregate state.
  if (now_aggregate_failed) {
    host->healthFlagSet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
  } else {
    host->healthFlagClear(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
  }

  if (now_aggregate_degraded) {
    host->healthFlagSet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC);
  } else {
    host->healthFlagClear(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC);
  }

  if (now_aggregate_pending) {
    host->healthFlagSet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC);
  } else {
    host->healthFlagClear(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC);
  }

  // Don't fire callbacks while all checkers are still pending initial checks.
  if (was_aggregate_pending && now_aggregate_pending) {
    return;
  }

  Upstream::HealthTransition aggregate_transition = Upstream::HealthTransition::Unchanged;
  if (was_aggregate_failed != now_aggregate_failed ||
      was_aggregate_degraded != now_aggregate_degraded ||
      was_aggregate_pending != now_aggregate_pending) {
    aggregate_transition = Upstream::HealthTransition::Changed;
  }

  Upstream::HealthState aggregate_result =
      now_aggregate_failed ? Upstream::HealthState::Unhealthy : Upstream::HealthState::Healthy;

  for (const auto& cb : callbacks_) {
    cb(host, aggregate_transition, aggregate_result);
  }
}

// Factory implementation

Upstream::HealthCheckerSharedPtr MultiHealthCheckerFactory::createCustomHealthChecker(
    const envoy::config::core::v3::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {
  return std::make_shared<MultiHealthChecker>(context.cluster(), config,
                                              context.serverFactoryContext());
}

REGISTER_FACTORY(MultiHealthCheckerFactory, Server::Configuration::CustomHealthCheckerFactory);

} // namespace Multi
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
