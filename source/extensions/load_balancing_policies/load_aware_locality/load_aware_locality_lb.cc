#include "source/extensions/load_balancing_policies/load_aware_locality/load_aware_locality_lb.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {

// --- LoadAwareLocalityLoadBalancer (main thread) ---

LoadAwareLocalityLoadBalancer::LoadAwareLocalityLoadBalancer(
    OptRef<const Upstream::LoadBalancerConfig> lb_config, const Upstream::ClusterInfo& cluster_info,
    const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
    Envoy::Random::RandomGenerator& random, TimeSource& time_source)
    : priority_set_(priority_set) {
  const auto* typed_config = dynamic_cast<const LoadAwareLocalityLbConfig*>(lb_config.ptr());
  ASSERT(typed_config != nullptr);

  utilization_variance_threshold_ = typed_config->utilizationVarianceThreshold();
  ewma_alpha_ = typed_config->ewmaAlpha();
  probe_percentage_ = typed_config->probePercentage();
  weight_update_period_ = typed_config->weightUpdatePeriod();

  factory_ = std::make_shared<WorkerLocalLbFactory>(
      typed_config->endpointPickingPolicyFactory(), typed_config->endpointPickingPolicyConfig(),
      cluster_info, priority_set, runtime, random, time_source, typed_config->tlsSlotAllocator());

  weight_update_timer_ = typed_config->mainThreadDispatcher().createTimer(
      [this]() { computeLocalityRoutingWeights(); });
}

absl::Status LoadAwareLocalityLoadBalancer::initialize() {
  RETURN_IF_NOT_OK(factory_->initializeChildLb());
  computeLocalityRoutingWeights();
  return absl::OkStatus();
}

void LoadAwareLocalityLoadBalancer::computeLocalityRoutingWeights() {
  // Re-arm first so the timer always fires on schedule regardless of early returns below.
  weight_update_timer_->enableTimer(weight_update_period_);

  if (priority_set_.hostSetsPerPriority().empty()) {
    return;
  }

  const auto& host_set = priority_set_.hostSetsPerPriority()[0];
  const auto& hosts_per_locality = host_set->hostsPerLocality();
  const auto& locality_hosts = hosts_per_locality.get();
  const auto& healthy_hosts_per_locality = host_set->healthyHostsPerLocality().get();

  if (locality_hosts.empty()) {
    return;
  }

  const size_t locality_count = locality_hosts.size();

  // Step 1: Compute raw per-locality utilization and healthy host counts.
  avg_utils_.assign(locality_count, 0.0);
  valid_counts_.assign(locality_count, 0);
  host_counts_.assign(locality_count, 0);

  for (size_t i = 0; i < locality_count; ++i) {
    // Use healthy host count for capacity weighting so that localities with many
    // degraded/excluded hosts don't appear to have more capacity than they actually do.
    // Fall back to 0 (not all-host count) when healthy data is missing — it's safer to
    // under-estimate capacity than over-estimate it.
    const bool has_healthy = (i < healthy_hosts_per_locality.size());
    host_counts_[i] =
        has_healthy ? static_cast<uint32_t>(healthy_hosts_per_locality[i].size()) : 0u;

    // Only sample utilization from healthy hosts. Unhealthy hosts may carry stale high-util
    // readings that would make the locality appear more loaded than it actually is.
    double util_sum = 0.0;
    uint32_t valid_count = 0;
    if (has_healthy) {
      for (const auto& host : healthy_hosts_per_locality[i]) {
        const double util = host->orcaUtilization().get();
        if (util > 0.0) {
          util_sum += util;
          valid_count++;
        }
      }
    }

    avg_utils_[i] = valid_count > 0 ? util_sum / valid_count : 0.0;
    valid_counts_[i] = valid_count;
  }

  // Step 2: EWMA smoothing.
  // Only initialize smoothed state when we have actual ORCA data, so the first tick
  // with data uses raw values (true cold start) rather than blending against 0.
  const bool has_any_valid_data =
      std::any_of(valid_counts_.begin(), valid_counts_.end(), [](uint32_t c) { return c > 0; });

  if (smoothed_utilizations_.size() != locality_count) {
    if (has_any_valid_data) {
      // Cold start or topology change with actual data: initialize from raw values.
      smoothed_utilizations_ = avg_utils_;
    }
    // If no valid data yet, don't initialize — use raw avg_utils_ for weights below.
  } else if (has_any_valid_data) {
    for (size_t i = 0; i < locality_count; ++i) {
      if (valid_counts_[i] > 0) {
        smoothed_utilizations_[i] =
            ewma_alpha_ * avg_utils_[i] + (1.0 - ewma_alpha_) * smoothed_utilizations_[i];
      }
      // If no ORCA data (valid_counts_[i] == 0): retain previous smoothed value.
    }
  }

  // Use smoothed values if available, otherwise raw averages.
  const std::vector<double>& utilizations =
      (smoothed_utilizations_.size() == locality_count) ? smoothed_utilizations_ : avg_utils_;

  // Step 3: Host-count-weighted headroom. Also accumulate total_hosts for steps 4 and 6.
  auto snapshot = std::make_shared<RoutingWeightsSnapshot>();
  snapshot->weights.resize(locality_count, 0.0);
  uint32_t total_hosts = 0;

  for (size_t i = 0; i < locality_count; ++i) {
    snapshot->weights[i] = host_counts_[i] * std::max(0.0, 1.0 - utilizations[i]);
    total_hosts += host_counts_[i];
  }

  // Step 4: Variance threshold check (local preference).
  const auto setAllLocal = [&snapshot]() {
    snapshot->all_local = true;
    std::fill(snapshot->weights.begin(), snapshot->weights.end(), 0.0);
    snapshot->weights[0] = 1.0;
  };

  if (hosts_per_locality.hasLocalLocality()) {
    // Compute host-count-weighted remote average utilization (target_util).
    // Excludes the local locality so that local's own load does not inflate
    // the average and make the threshold harder to trigger.
    double remote_util_sum = 0.0;
    uint32_t remote_hosts = 0;
    for (size_t i = 1; i < locality_count; ++i) {
      remote_util_sum += utilizations[i] * host_counts_[i];
      remote_hosts += host_counts_[i];
    }

    if (total_hosts > 0 && snapshot->weights[0] > 0.0) {
      double target_util = remote_hosts > 0 ? remote_util_sum / remote_hosts : 0.0;
      if (utilizations[0] <= target_util + utilization_variance_threshold_) {
        setAllLocal();
      }
    } else if (total_hosts == 0) {
      // No data at all — default to local.
      setAllLocal();
    }
  }

  // Step 5: Probe percentage — ensure remotes get a minimum traffic share.
  if (hosts_per_locality.hasLocalLocality() && probe_percentage_ > 0.0 && locality_count > 1) {
    double total = 0.0;
    for (double w : snapshot->weights) {
      total += w;
    }
    if (total > 0.0) {
      double remote_sum = total - snapshot->weights[0];
      double remote_target = total * probe_percentage_;

      if (remote_sum < remote_target) {
        // Count remote healthy hosts only when redistribution is needed. If there are none,
        // skip to avoid deducting weight from local without giving it to anyone.
        uint32_t remote_hosts = 0;
        for (size_t i = 1; i < locality_count; ++i) {
          remote_hosts += host_counts_[i];
        }

        if (remote_hosts > 0) {
          double deficit = remote_target - remote_sum;
          snapshot->weights[0] = std::max(0.0, snapshot->weights[0] - deficit);

          // Distribute deficit to remotes proportional to healthy host count.
          for (size_t i = 1; i < locality_count; ++i) {
            snapshot->weights[i] += deficit * static_cast<double>(host_counts_[i]) / remote_hosts;
          }
        }
      }
    }
  }

  // Compute total_weight once from final weights. Also handles the all-overloaded fallback:
  // if every locality is at 100% utilization, all headroom == 0 and total_weight == 0.
  // Rather than concentrating all traffic on locality 0, distribute proportionally to
  // healthy host count so that each locality carries its fair share of the load.
  snapshot->total_weight = 0.0;
  for (double w : snapshot->weights) {
    snapshot->total_weight += w;
  }
  if (snapshot->total_weight == 0.0 && total_hosts > 0) {
    for (size_t i = 0; i < locality_count; ++i) {
      snapshot->weights[i] = static_cast<double>(host_counts_[i]);
    }
    snapshot->total_weight = static_cast<double>(total_hosts);
  }

  ENVOY_LOG(trace, "computeLocalityRoutingWeights: {} localities, total_weight={}, all_local={}",
            locality_count, snapshot->total_weight, snapshot->all_local);
  factory_->updateRoutingWeights(std::move(snapshot));
}

// --- WorkerLocalLbFactory ---

WorkerLocalLbFactory::WorkerLocalLbFactory(
    Upstream::TypedLoadBalancerFactory& child_factory, LoadBalancerConfigSharedPtr child_config,
    const Upstream::ClusterInfo& cluster_info, const Upstream::PrioritySet& cluster_priority_set,
    Runtime::Loader& runtime, Envoy::Random::RandomGenerator& random, TimeSource& time_source,
    ThreadLocal::SlotAllocator& tls_slot_allocator)
    : child_factory_(child_factory), child_config_(std::move(child_config)),
      cluster_info_(cluster_info), runtime_(runtime), random_(random), time_source_(time_source) {
  auto child_config_ref =
      makeOptRefFromPtr<const Upstream::LoadBalancerConfig>(child_config_.get());
  child_thread_aware_lb_ = child_factory_.create(
      child_config_ref, cluster_info_, cluster_priority_set, runtime_, random_, time_source_);
  tls_ = ThreadLocal::TypedSlot<ThreadLocalShim>::makeUnique(tls_slot_allocator);
  tls_->set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalShim>(); });
}

absl::Status WorkerLocalLbFactory::initializeChildLb() {
  ASSERT(child_thread_aware_lb_ != nullptr);
  return child_thread_aware_lb_->initialize();
}

Upstream::LoadBalancerPtr
WorkerLocalLbFactory::createWorkerChildLb(Upstream::PrioritySetImpl& per_locality_priority_set) {
  // initializeChildLb() must have been called on the main thread before workers call this.
  ASSERT(child_thread_aware_lb_ != nullptr);
  Upstream::LoadBalancerParams child_params{per_locality_priority_set, nullptr};
  return child_thread_aware_lb_->factory()->create(child_params);
}

bool WorkerLocalLbFactory::recreateChildOnHostChange() const {
  ASSERT(child_thread_aware_lb_ != nullptr);
  return child_thread_aware_lb_->factory()->recreateOnHostChange();
}

Upstream::LoadBalancerPtr WorkerLocalLbFactory::create(Upstream::LoadBalancerParams params) {
  return std::make_unique<WorkerLocalLb>(*this, params.priority_set);
}

// --- WorkerLocalLb (per-worker) ---

WorkerLocalLb::WorkerLocalLb(WorkerLocalLbFactory& factory,
                             const Upstream::PrioritySet& priority_set)
    : factory_(factory), priority_set_(priority_set) {
  if (!priority_set_.hostSetsPerPriority().empty()) {
    buildPerLocality(priority_set_.hostSetsPerPriority()[0]->hostsPerLocality());
  }
  // Register AFTER initial build so callback doesn't fire during construction.
  member_update_cb_ = priority_set_.addPriorityUpdateCb(
      [this](uint32_t priority, const Upstream::HostVector& hosts_added,
             const Upstream::HostVector& hosts_removed) {
        if (!hosts_added.empty() || !hosts_removed.empty()) {
          onHostChange(priority);
        } else {
          // Health-only update (no membership change). Re-partition each locality so child
          // LBs see current healthy/degraded/excluded host sets.
          onHealthChange(priority);
        }
      });
}

void WorkerLocalLb::updateLocalityHosts(PerLocalityState& state, const Upstream::HostVector& hosts,
                                        bool is_local, const Upstream::HostVector& hosts_added,
                                        const Upstream::HostVector& hosts_removed) {
  auto hosts_shared = std::make_shared<Upstream::HostVector>(hosts);
  auto per_locality = std::make_shared<Upstream::HostsPerLocalityImpl>(hosts, is_local);
  auto update_params = Upstream::HostSetImpl::partitionHosts(hosts_shared, per_locality);
  state.priority_set->updateHosts(0, std::move(update_params), nullptr, hosts_added, hosts_removed,
                                  absl::nullopt, absl::nullopt);
}

void WorkerLocalLb::buildPerLocality(const Upstream::HostsPerLocality& hosts_per_locality) {
  const auto& locality_hosts = hosts_per_locality.get();
  per_locality_.clear();
  per_locality_.reserve(locality_hosts.size());

  for (size_t i = 0; i < locality_hosts.size(); ++i) {
    const auto& hosts = locality_hosts[i];

    PerLocalityState state;
    state.priority_set = std::make_unique<Upstream::PrioritySetImpl>();

    // Locality 0 is the local locality when the cluster has a local locality. Pass that flag
    // through so child policies that check hasLocalLocality() see the correct value.
    const bool is_local = (i == 0 && hosts_per_locality.hasLocalLocality());
    updateLocalityHosts(state, hosts, is_local, hosts, {});

    state.lb = factory_.createWorkerChildLb(*state.priority_set);

    per_locality_.push_back(std::move(state));
  }
}

void WorkerLocalLb::onHostChange(uint32_t priority) {
  if (priority != 0) {
    return;
  }

  const auto& host_set = priority_set_.hostSetsPerPriority()[0];
  const auto& hosts_per_locality = host_set->hostsPerLocality();
  const auto& locality_hosts = hosts_per_locality.get();

  // Topology change (locality added/removed) — full rebuild.
  if (locality_hosts.size() != per_locality_.size()) {
    buildPerLocality(hosts_per_locality);
    return;
  }

  // Per-locality incremental update.
  const bool recreate_child = factory_.recreateChildOnHostChange();
  for (size_t i = 0; i < locality_hosts.size(); ++i) {
    const auto& new_hosts = locality_hosts[i];
    auto& state = per_locality_[i];

    // Diff old vs new: build one set from old hosts, erase matches from new hosts.
    const auto& old_hosts = state.priority_set->hostSetsPerPriority()[0]->hosts();
    absl::flat_hash_set<Upstream::HostConstSharedPtr> old_set(old_hosts.begin(), old_hosts.end());

    Upstream::HostVector hosts_added, hosts_removed;
    for (const auto& h : new_hosts) {
      if (!old_set.erase(h)) {
        hosts_added.push_back(h);
      }
    }
    // Remaining entries in old_set were not in new_hosts — they were removed.
    hosts_removed.reserve(old_set.size());
    for (const auto& h : old_set) {
      hosts_removed.push_back(std::const_pointer_cast<Upstream::Host>(h));
    }

    if (hosts_added.empty() && hosts_removed.empty()) {
      continue;
    }

    const bool is_local = (i == 0 && hosts_per_locality.hasLocalLocality());
    updateLocalityHosts(state, new_hosts, is_local, hosts_added, hosts_removed);

    // If child policy says recreateOnHostChange (default for RR), recreate worker LB.
    if (recreate_child) {
      state.lb = factory_.createWorkerChildLb(*state.priority_set);
    }
    // Otherwise child LB handles it via its own registered callback on state.priority_set.
  }
}

void WorkerLocalLb::onHealthChange(uint32_t priority) {
  if (priority != 0) {
    return;
  }

  const auto& host_set = priority_set_.hostSetsPerPriority()[0];
  const auto& hosts_per_locality = host_set->hostsPerLocality();
  const auto& locality_hosts = hosts_per_locality.get();

  // Topology mismatch — wait for onHostChange to rebuild.
  if (locality_hosts.size() != per_locality_.size()) {
    return;
  }

  const bool recreate_child = factory_.recreateChildOnHostChange();
  for (size_t i = 0; i < locality_hosts.size(); ++i) {
    auto& state = per_locality_[i];
    const bool is_local = (i == 0 && hosts_per_locality.hasLocalLocality());
    // Re-partition with current health status. Empty added/removed since membership is unchanged.
    updateLocalityHosts(state, locality_hosts[i], is_local, {}, {});

    if (recreate_child) {
      state.lb = factory_.createWorkerChildLb(*state.priority_set);
    }
  }
}

Upstream::HostSelectionResponse WorkerLocalLb::chooseHost(Upstream::LoadBalancerContext* context) {
  if (per_locality_.empty()) {
    return {nullptr};
  }
  return per_locality_[chooseLocality()].lb->chooseHost(context);
}

size_t WorkerLocalLb::chooseLocality() {
  if (per_locality_.size() <= 1) {
    return 0;
  }
  const auto* weights = factory_.routingWeights();
  if (!weights) {
    return 0;
  }
  return selectLocality(*weights);
}

size_t WorkerLocalLb::selectLocality(const RoutingWeightsSnapshot& snapshot) {
  if (snapshot.total_weight <= 0.0 || snapshot.weights.empty()) {
    return 0;
  }

  // Clamp to the valid range in case snapshot and per_locality_ have different counts
  // (transient window between a topology change and the next weight update).
  const size_t num_localities = std::min(snapshot.weights.size(), per_locality_.size());

  // Use the pre-computed total when counts agree. Only re-sum over the clamped range
  // during a transient topology mismatch, otherwise stale snapshot entries would spill
  // probability mass onto the last locality.
  double effective_total;
  if (num_localities == snapshot.weights.size()) {
    effective_total = snapshot.total_weight;
  } else {
    effective_total = 0.0;
    for (size_t i = 0; i < num_localities; ++i) {
      effective_total += snapshot.weights[i];
    }
  }
  if (effective_total <= 0.0) {
    return 0;
  }

  auto& rng = factory_.random();
  double target = (rng.random() / static_cast<double>(rng.max())) * effective_total;
  double cumulative = 0.0;
  for (size_t i = 0; i < num_localities; ++i) {
    cumulative += snapshot.weights[i];
    if (target < cumulative) {
      return i;
    }
  }

  // Floating-point rounding guard: return the last clamped locality, not an arbitrary one.
  return num_localities - 1;
}

Upstream::HostConstSharedPtr
WorkerLocalLb::peekAnotherHost(Upstream::LoadBalancerContext* context) {
  if (per_locality_.empty()) {
    return nullptr;
  }
  return per_locality_[chooseLocality()].lb->peekAnotherHost(context);
}

OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks>
WorkerLocalLb::lifetimeCallbacks() {
  return {};
}

absl::optional<Upstream::SelectedPoolAndConnection>
WorkerLocalLb::selectExistingConnection(Upstream::LoadBalancerContext*, const Upstream::Host&,
                                        std::vector<uint8_t>&) {
  return absl::nullopt;
}

} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
