#include "source/extensions/health_checkers/multi/multi.h"

#include "envoy/registry/registry.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/upstream/health_checker_impl.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace Multi {

// HealthCheckHostProxy implementation

HealthCheckHostProxy::HealthCheckHostProxy(Upstream::HostSharedPtr real_host)
    : real_host_(std::move(real_host)),
      local_flags_(real_host_->healthFlagsGetAll() & kInterceptedMask) {}

bool HealthCheckHostProxy::isInterceptedFlag(HealthFlag flag) {
  return (static_cast<uint32_t>(flag) & kInterceptedMask) != 0;
}

void HealthCheckHostProxy::healthFlagSet(HealthFlag flag) {
  if (isInterceptedFlag(flag)) {
    local_flags_ |= static_cast<uint32_t>(flag);
  } else {
    real_host_->healthFlagSet(flag);
  }
}

bool HealthCheckHostProxy::healthFlagGet(HealthFlag flag) const {
  if (isInterceptedFlag(flag)) {
    return (local_flags_.load(std::memory_order_relaxed) & static_cast<uint32_t>(flag)) != 0;
  }
  return real_host_->healthFlagGet(flag);
}

void HealthCheckHostProxy::healthFlagClear(HealthFlag flag) {
  if (isInterceptedFlag(flag)) {
    local_flags_ &= ~static_cast<uint32_t>(flag);
  } else {
    real_host_->healthFlagClear(flag);
  }
}

uint32_t HealthCheckHostProxy::healthFlagsGetAll() const {
  uint32_t real = real_host_->healthFlagsGetAll() & ~kInterceptedMask;
  return real | local_flags_.load(std::memory_order_relaxed);
}

void HealthCheckHostProxy::healthFlagsSetAll(uint32_t bits) {
  local_flags_ |= (bits & kInterceptedMask);
  uint32_t non_intercepted = bits & ~kInterceptedMask;
  if (non_intercepted != 0) {
    real_host_->healthFlagsSetAll(non_intercepted);
  }
}

// Host interface delegation

std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>>
HealthCheckHostProxy::counters() const {
  return real_host_->counters();
}

Upstream::Host::CreateConnectionData HealthCheckHostProxy::createConnection(
    Event::Dispatcher& dispatcher, const Network::ConnectionSocket::OptionsSharedPtr& options,
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const {
  return real_host_->createConnection(dispatcher, options, transport_socket_options);
}

Upstream::Host::CreateConnectionData HealthCheckHostProxy::createHealthCheckConnection(
    Event::Dispatcher& dispatcher,
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options,
    const envoy::config::core::v3::Metadata* metadata) const {
  return real_host_->createHealthCheckConnection(dispatcher, transport_socket_options, metadata);
}

std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>>
HealthCheckHostProxy::gauges() const {
  return real_host_->gauges();
}

Upstream::Host::Health HealthCheckHostProxy::coarseHealth() const {
  return real_host_->coarseHealth();
}

Upstream::Host::HealthStatus HealthCheckHostProxy::healthStatus() const {
  return real_host_->healthStatus();
}

void HealthCheckHostProxy::setEdsHealthStatus(HealthStatus health_status) {
  real_host_->setEdsHealthStatus(health_status);
}

Upstream::Host::HealthStatus HealthCheckHostProxy::edsHealthStatus() const {
  return real_host_->edsHealthStatus();
}

uint32_t HealthCheckHostProxy::weight() const { return real_host_->weight(); }
void HealthCheckHostProxy::weight(uint32_t new_weight) { real_host_->weight(new_weight); }
bool HealthCheckHostProxy::used() const { return real_host_->used(); }

Upstream::HostHandlePtr HealthCheckHostProxy::acquireHandle() const {
  return real_host_->acquireHandle();
}

bool HealthCheckHostProxy::disableActiveHealthCheck() const {
  return real_host_->disableActiveHealthCheck();
}

void HealthCheckHostProxy::setDisableActiveHealthCheck(bool disable_active_health_check) {
  real_host_->setDisableActiveHealthCheck(disable_active_health_check);
}

// HostDescription interface delegation

bool HealthCheckHostProxy::canary() const { return real_host_->canary(); }
void HealthCheckHostProxy::canary(bool is_canary) { real_host_->canary(is_canary); }

Upstream::MetadataConstSharedPtr HealthCheckHostProxy::metadata() const {
  return real_host_->metadata();
}

std::size_t HealthCheckHostProxy::metadataHash() const { return real_host_->metadataHash(); }

void HealthCheckHostProxy::metadata(Upstream::MetadataConstSharedPtr new_metadata) {
  real_host_->metadata(std::move(new_metadata));
}

const Upstream::ClusterInfo& HealthCheckHostProxy::cluster() const {
  return real_host_->cluster();
}

bool HealthCheckHostProxy::canCreateConnection(Upstream::ResourcePriority priority) const {
  return real_host_->canCreateConnection(priority);
}

Upstream::Outlier::DetectorHostMonitor& HealthCheckHostProxy::outlierDetector() const {
  return real_host_->outlierDetector();
}

void HealthCheckHostProxy::setOutlierDetector(
    Upstream::Outlier::DetectorHostMonitorPtr&& outlier_detector) {
  real_host_->setOutlierDetector(std::move(outlier_detector));
}

Upstream::HealthCheckHostMonitor& HealthCheckHostProxy::healthChecker() const {
  if (health_checker_) {
    return *health_checker_;
  }
  return real_host_->healthChecker();
}

void HealthCheckHostProxy::setHealthChecker(Upstream::HealthCheckHostMonitorPtr&& health_checker) {
  health_checker_ = std::move(health_checker);
}

const std::string& HealthCheckHostProxy::hostnameForHealthChecks() const {
  return real_host_->hostnameForHealthChecks();
}

const std::string& HealthCheckHostProxy::hostname() const { return real_host_->hostname(); }

Network::UpstreamTransportSocketFactory& HealthCheckHostProxy::transportSocketFactory() const {
  return real_host_->transportSocketFactory();
}

Network::Address::InstanceConstSharedPtr HealthCheckHostProxy::address() const {
  return real_host_->address();
}

Upstream::HostDescription::SharedConstAddressVector
HealthCheckHostProxy::addressListOrNull() const {
  return real_host_->addressListOrNull();
}

Upstream::HostStats& HealthCheckHostProxy::stats() const { return real_host_->stats(); }

Upstream::LoadMetricStats& HealthCheckHostProxy::loadMetricStats() const {
  return real_host_->loadMetricStats();
}

const envoy::config::core::v3::Locality& HealthCheckHostProxy::locality() const {
  return real_host_->locality();
}

const Upstream::MetadataConstSharedPtr HealthCheckHostProxy::localityMetadata() const {
  return real_host_->localityMetadata();
}

Stats::StatName HealthCheckHostProxy::localityZoneStatName() const {
  return real_host_->localityZoneStatName();
}

Network::Address::InstanceConstSharedPtr HealthCheckHostProxy::healthCheckAddress() const {
  return real_host_->healthCheckAddress();
}

uint32_t HealthCheckHostProxy::priority() const { return real_host_->priority(); }
void HealthCheckHostProxy::priority(uint32_t p) { real_host_->priority(p); }

absl::optional<MonotonicTime> HealthCheckHostProxy::lastHcPassTime() const {
  return real_host_->lastHcPassTime();
}

void HealthCheckHostProxy::setLastHcPassTime(MonotonicTime last_hc_pass_time) {
  real_host_->setLastHcPassTime(last_hc_pass_time);
}

Network::UpstreamTransportSocketFactory& HealthCheckHostProxy::resolveTransportSocketFactory(
    const Network::Address::InstanceConstSharedPtr& dest_address,
    const envoy::config::core::v3::Metadata* metadata,
    Network::TransportSocketOptionsConstSharedPtr transport_socket_options) const {
  return real_host_->resolveTransportSocketFactory(dest_address, metadata,
                                                   transport_socket_options);
}

void HealthCheckHostProxy::setLbPolicyData(Upstream::HostLbPolicyDataPtr lb_policy_data) {
  real_host_->setLbPolicyData(std::move(lb_policy_data));
}

OptRef<Upstream::HostLbPolicyData> HealthCheckHostProxy::lbPolicyData() const {
  return real_host_->lbPolicyData();
}

// ProxyHostSet implementation

void ProxyHostSet::setHosts(Upstream::HostVector hosts) {
  hosts_ = std::move(hosts);
  hosts_ptr_ = std::make_shared<Upstream::HostVector>(hosts_);
  healthy_hosts_ptr_ = std::make_shared<Upstream::HealthyHostVector>(hosts_);
}

// ProxyPrioritySet implementation

ProxyHostSet& ProxyPrioritySet::getOrCreateHostSet(uint32_t priority) {
  while (host_sets_.size() <= priority) {
    host_sets_.push_back(std::make_unique<ProxyHostSet>(host_sets_.size()));
  }
  return static_cast<ProxyHostSet&>(*host_sets_[priority]);
}

void ProxyPrioritySet::updateHosts(uint32_t priority,
                                   UpdateHostsParams&& /*update_hosts_params*/,
                                   Upstream::LocalityWeightsConstSharedPtr /*locality_weights*/,
                                   const Upstream::HostVector& hosts_added,
                                   const Upstream::HostVector& hosts_removed,
                                   absl::optional<bool> /*weighted_priority_health*/,
                                   absl::optional<uint32_t> /*overprovisioning_factor*/,
                                   Upstream::HostMapConstSharedPtr /*cross_priority_host_map*/) {
  updateHosts(priority, hosts_added, hosts_removed);
}

void ProxyPrioritySet::batchHostUpdate(BatchUpdateCb& /*callback*/) {
  // Not needed for health checking.
}

void ProxyPrioritySet::updateHosts(uint32_t priority, const Upstream::HostVector& hosts_added,
                                   const Upstream::HostVector& hosts_removed) {
  auto& host_set = getOrCreateHostSet(priority);

  // Get the current hosts and apply the update.
  Upstream::HostVector current = host_set.hosts();
  for (const auto& host : hosts_removed) {
    current.erase(std::remove(current.begin(), current.end(), host), current.end());
  }
  for (const auto& host : hosts_added) {
    current.push_back(host);
  }
  host_set.setHosts(std::move(current));

  member_update_cb_helper_.runCallbacks(hosts_added, hosts_removed);
  priority_update_cb_helper_.runCallbacks(priority, hosts_added, hosts_removed);
}

// MultiHealthChecker implementation

MultiHealthChecker::MultiHealthChecker(
    Upstream::Cluster& cluster,
    Server::Configuration::ServerFactoryContext& server_context,
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck>& health_check_configs)
    : cluster_(cluster) {

  for (uint32_t i = 0; i < static_cast<uint32_t>(health_check_configs.size()); i++) {
    const auto& hc_config = health_check_configs[i];
    const std::string effective_stat_prefix =
        hc_config.stat_prefix().empty() ? std::to_string(i) : hc_config.stat_prefix();

    PerCheckerData data;
    data.stat_prefix = effective_stat_prefix;
    data.proxy_cluster = std::make_unique<ProxyCluster>(cluster);

    auto checker_or_error =
        Upstream::HealthCheckerFactory::create(hc_config, *data.proxy_cluster, server_context);
    THROW_IF_NOT_OK(checker_or_error.status());
    data.checker = checker_or_error.value();

    const uint32_t index = i;
    data.checker->addHostCheckCompleteCb(
        [this, index](const Upstream::HostSharedPtr& host,
                      Upstream::HealthTransition changed_state, Upstream::HealthState result) {
          onCheckerResult(index, host, changed_state, result);
        });

    checker_data_.push_back(std::move(data));
  }

  member_update_cb_ = cluster_.prioritySet().addMemberUpdateCb(
      [this](const Upstream::HostVector& hosts_added, const Upstream::HostVector& hosts_removed) {
        onClusterMemberUpdate(hosts_added, hosts_removed);
      });
}

void MultiHealthChecker::addHostCheckCompleteCb(HostStatusCb callback) {
  callbacks_.push_back(callback);
}

void MultiHealthChecker::start() {
  // Populate proxy hosts from all existing hosts in the real cluster.
  for (const auto& host_set : cluster_.prioritySet().hostSetsPerPriority()) {
    populateProxyHosts(host_set->hosts());
  }

  // Start all inner checkers — they will discover hosts via their proxy priority sets.
  for (auto& data : checker_data_) {
    data.checker->start();
  }
}

void MultiHealthChecker::populateProxyHosts(const Upstream::HostVector& hosts) {
  for (const auto& real_host : hosts) {
    for (auto& data : checker_data_) {
      auto proxy = std::make_shared<HealthCheckHostProxy>(real_host);
      data.proxy_hosts[real_host.get()] = proxy;
    }
  }

  // Notify each proxy priority set grouped by priority level.
  absl::flat_hash_map<uint32_t, Upstream::HostVector> by_priority;
  for (const auto& host : hosts) {
    by_priority[host->priority()].push_back(host);
  }

  for (auto& [priority, priority_hosts] : by_priority) {
    for (auto& data : checker_data_) {
      Upstream::HostVector proxy_hosts;
      proxy_hosts.reserve(priority_hosts.size());
      for (const auto& h : priority_hosts) {
        proxy_hosts.push_back(data.proxy_hosts[h.get()]);
      }
      data.proxy_cluster->proxyPrioritySet().updateHosts(priority, proxy_hosts, {});
    }
  }
}

void MultiHealthChecker::removeProxyHosts(const Upstream::HostVector& hosts) {
  absl::flat_hash_map<uint32_t, Upstream::HostVector> by_priority;
  for (const auto& host : hosts) {
    by_priority[host->priority()].push_back(host);
  }

  for (auto& [priority, priority_hosts] : by_priority) {
    for (auto& data : checker_data_) {
      Upstream::HostVector proxy_hosts_removed;
      proxy_hosts_removed.reserve(priority_hosts.size());
      for (const auto& h : priority_hosts) {
        auto it = data.proxy_hosts.find(h.get());
        if (it != data.proxy_hosts.end()) {
          proxy_hosts_removed.push_back(it->second);
          data.proxy_hosts.erase(it);
        }
      }
      data.proxy_cluster->proxyPrioritySet().updateHosts(priority, {},
                                                         proxy_hosts_removed);
    }
  }

  for (const auto& host : hosts) {
    host_states_.erase(host.get());
  }
}

void MultiHealthChecker::onClusterMemberUpdate(const Upstream::HostVector& hosts_added,
                                               const Upstream::HostVector& hosts_removed) {
  populateProxyHosts(hosts_added);
  removeProxyHosts(hosts_removed);
}

void MultiHealthChecker::onCheckerResult(uint32_t checker_index,
                                         const Upstream::HostSharedPtr& proxy_host,
                                         Upstream::HealthTransition changed_state,
                                         Upstream::HealthState result) {
  // The callback receives the proxy host. Find the real host.
  auto* proxy = dynamic_cast<HealthCheckHostProxy*>(proxy_host.get());
  if (proxy == nullptr) {
    return;
  }
  Upstream::HostSharedPtr real_host = proxy->realHost();

  const uint32_t bit = 1u << checker_index;
  auto [it, inserted] = host_states_.try_emplace(real_host.get(), PerHostState{});
  if (inserted) {
    const uint32_t all_bits = (1u << checker_data_.size()) - 1;
    it->second.fail_bits = all_bits;
    it->second.pending_bits = all_bits;
  }
  auto& state = it->second;

  const bool was_aggregate_failed = state.fail_bits != 0;
  const bool was_aggregate_degraded = state.degraded_bits != 0;
  const bool was_aggregate_pending = state.pending_bits != 0;

  // Update fail bits based on per-checker result.
  if (changed_state == Upstream::HealthTransition::Changed) {
    if (result == Upstream::HealthState::Unhealthy) {
      state.fail_bits |= bit;
    } else {
      state.fail_bits &= ~bit;
    }
  }

  // Update degraded bits from proxy host flags.
  if (proxy->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC)) {
    state.degraded_bits |= bit;
  } else {
    state.degraded_bits &= ~bit;
  }

  // Clear pending bit for this checker.
  state.pending_bits &= ~bit;

  const bool now_aggregate_failed = state.fail_bits != 0;
  const bool now_aggregate_degraded = state.degraded_bits != 0;
  const bool now_aggregate_pending = state.pending_bits != 0;

  // Apply aggregated flags to the real host.
  if (now_aggregate_failed) {
    real_host->healthFlagSet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
  } else {
    real_host->healthFlagClear(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
  }

  if (now_aggregate_degraded) {
    real_host->healthFlagSet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC);
  } else {
    real_host->healthFlagClear(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC);
  }

  if (now_aggregate_pending) {
    real_host->healthFlagSet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC);
  } else {
    real_host->healthFlagClear(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC);
  }

  // Don't fire callbacks while all checkers are still pending.
  if (was_aggregate_pending && now_aggregate_pending) {
    return;
  }

  Upstream::HealthTransition aggregate_transition = Upstream::HealthTransition::Unchanged;
  if (was_aggregate_failed != now_aggregate_failed ||
      was_aggregate_degraded != now_aggregate_degraded ||
      was_aggregate_pending != now_aggregate_pending) {
    aggregate_transition = Upstream::HealthTransition::Changed;
  } else if (changed_state == Upstream::HealthTransition::ChangePending) {
    aggregate_transition = Upstream::HealthTransition::ChangePending;
  }

  Upstream::HealthState aggregate_result =
      now_aggregate_failed ? Upstream::HealthState::Unhealthy : Upstream::HealthState::Healthy;

  for (const auto& cb : callbacks_) {
    cb(real_host, aggregate_transition, aggregate_result);
  }
}

std::vector<PerCheckerStatus>
MultiHealthChecker::perCheckerStatus(const Upstream::Host& host) const {
  std::vector<PerCheckerStatus> result;
  result.reserve(checker_data_.size());
  for (const auto& data : checker_data_) {
    PerCheckerStatus info;
    info.stat_prefix = data.stat_prefix;
    auto it = data.proxy_hosts.find(&host);
    if (it != data.proxy_hosts.end()) {
      info.failed = it->second->healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC);
      info.degraded = it->second->healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC);
      info.pending = it->second->healthFlagGet(Upstream::Host::HealthFlag::PENDING_ACTIVE_HC);
    } else {
      info.failed = true;
      info.pending = true;
    }
    result.push_back(std::move(info));
  }
  return result;
}

// Factory implementation

Upstream::HealthCheckerSharedPtr MultiHealthCheckerFactory::createCustomHealthChecker(
    const envoy::config::core::v3::HealthCheck& config,
    Server::Configuration::HealthCheckerFactoryContext& context) {
  const auto& any_config = config.custom_health_check().typed_config();
  envoy::extensions::health_checkers::multi::v3::Multi multi_config;
  THROW_IF_NOT_OK(MessageUtil::unpackTo(any_config, multi_config));

  return std::make_shared<MultiHealthChecker>(context.cluster(), context.serverFactoryContext(),
                                              multi_config.health_checks());
}

REGISTER_FACTORY(MultiHealthCheckerFactory, Server::Configuration::CustomHealthCheckerFactory);

} // namespace Multi
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
