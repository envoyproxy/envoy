#include "source/extensions/load_balancing_policies/load_aware_locality/load_aware_locality_lb.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <numeric>

#include "source/common/protobuf/utility.h"
#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {

class PriorityLoadEvaluator : public Upstream::LoadBalancerBase {
public:
  PriorityLoadEvaluator(const Upstream::PrioritySet& priority_set, Upstream::ClusterLbStats& stats,
                        Runtime::Loader& runtime, Envoy::Random::RandomGenerator& random,
                        uint32_t healthy_panic_threshold)
      : LoadBalancerBase(priority_set, stats, runtime, random, healthy_panic_threshold) {}

  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext*) override {
    return {nullptr};
  }

  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
    return nullptr;
  }

  const Upstream::HealthyAndDegradedLoad& priorityLoad() const { return per_priority_load_; }
  const std::vector<bool>& priorityPanic() const { return per_priority_panic_; }
};

// --- LoadAwareLocalityLoadBalancer (main thread) ---

LoadAwareLocalityLoadBalancer::LoadAwareLocalityLoadBalancer(
    OptRef<const Upstream::LoadBalancerConfig> lb_config, const Upstream::ClusterInfo& cluster_info,
    const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
    Envoy::Random::RandomGenerator& random, TimeSource& time_source)
    : priority_set_(priority_set), stats_(cluster_info.lbStats()), time_source_(time_source) {
  const auto* typed_config = dynamic_cast<const LoadAwareLocalityLbConfig*>(lb_config.ptr());
  ASSERT(typed_config != nullptr);

  utilization_variance_threshold_ = typed_config->utilizationVarianceThreshold();
  ewma_alpha_ = typed_config->ewmaAlpha();
  probe_percentage_ = typed_config->probePercentage();
  weight_expiration_period_ = typed_config->weightExpirationPeriod();
  weight_update_period_ = typed_config->weightUpdatePeriod();
  priority_load_evaluator_ = std::make_unique<PriorityLoadEvaluator>(
      priority_set_, stats_, runtime, random,
      PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(cluster_info.lbConfig(),
                                                     healthy_panic_threshold, 100, 50));

  factory_ = std::make_shared<WorkerLocalLbFactory>(
      typed_config->endpointPickingPolicyFactory(), typed_config->endpointPickingPolicyName(),
      typed_config->endpointPickingPolicyConfig(), cluster_info, priority_set, runtime, random,
      time_source, typed_config->tlsSlotAllocator());

  weight_update_timer_ = typed_config->mainThreadDispatcher().createTimer(
      [this]() { computeLocalityRoutingWeights(); });
}

LoadAwareLocalityLoadBalancer::~LoadAwareLocalityLoadBalancer() = default;

absl::Status LoadAwareLocalityLoadBalancer::initialize() {
  RETURN_IF_NOT_OK(factory_->initializeChildLb());
  computeLocalityRoutingWeights();
  return absl::OkStatus();
}

void LoadAwareLocalityLoadBalancer::computeLocalityRoutingWeights() {
  // Re-arm first so the timer always fires on schedule regardless of early returns below.
  weight_update_timer_->enableTimer(weight_update_period_);
  stats_.lb_recalculate_zone_structures_.inc();

  auto snapshot = std::make_shared<RoutingWeightsSnapshot>();
  const auto& host_sets = priority_set_.hostSetsPerPriority();
  snapshot->priority_weights.resize(host_sets.size());
  snapshot->priority_loads = priority_load_evaluator_->priorityLoad();
  snapshot->priority_panic = priority_load_evaluator_->priorityPanic();

  for (auto& per_source_smoothed : smoothed_utilizations_) {
    per_source_smoothed.resize(host_sets.size());
  }
  for (auto& per_source_valid : smoothed_utilizations_valid_) {
    per_source_valid.resize(host_sets.size());
  }

  // Current monotonic time in milliseconds, used for weight expiration checks.
  const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             time_source_.monotonicTime().time_since_epoch())
                             .count();

  const auto compute_source_weights =
      [this, now_ms](const Upstream::HostsPerLocality& all_hosts_per_locality,
                     const std::vector<Upstream::HostVector>& eligible_hosts_per_locality,
                     std::vector<double>& weights, double& total_weight, bool& all_local,
                     std::vector<double>& smoothed, std::vector<bool>& smoothed_valid) {
        const auto& locality_hosts = all_hosts_per_locality.get();
        const size_t locality_count = locality_hosts.size();
        weights.assign(locality_count, 0.0);
        total_weight = 0.0;
        all_local = false;

        avg_utils_.assign(locality_count, 0.0);
        valid_counts_.assign(locality_count, 0);
        host_counts_.assign(locality_count, 0);

        for (size_t i = 0; i < locality_count; ++i) {
          const bool has_eligible = i < eligible_hosts_per_locality.size();
          host_counts_[i] =
              has_eligible ? static_cast<uint32_t>(eligible_hosts_per_locality[i].size()) : 0u;

          double util_sum = 0.0;
          uint32_t valid_count = 0;
          if (has_eligible) {
            for (const auto& host : eligible_hosts_per_locality[i]) {
              const int64_t last_update_ms = host->orcaUtilization().lastUpdateTimeMs();
              if (last_update_ms == 0) {
                continue;
              }

              if (weight_expiration_period_.count() > 0 &&
                  (now_ms - last_update_ms) > weight_expiration_period_.count()) {
                continue;
              }

              util_sum += host->orcaUtilization().get();
              valid_count++;
            }
          }

          avg_utils_[i] = valid_count > 0 ? util_sum / valid_count : 0.0;
          valid_counts_[i] = valid_count;
        }

        if (smoothed.size() != locality_count || smoothed_valid.size() != locality_count) {
          smoothed.assign(locality_count, 0.0);
          smoothed_valid.assign(locality_count, false);
        }

        std::vector<double> utilizations(locality_count, 0.0);
        for (size_t i = 0; i < locality_count; ++i) {
          if (valid_counts_[i] > 0) {
            if (!smoothed_valid[i]) {
              smoothed[i] = avg_utils_[i];
              smoothed_valid[i] = true;
            } else {
              smoothed[i] = ewma_alpha_ * avg_utils_[i] + (1.0 - ewma_alpha_) * smoothed[i];
            }
          } else {
            // Expired or missing ORCA data should stop influencing routing until fresh data
            // arrives.
            smoothed[i] = 0.0;
            smoothed_valid[i] = false;
          }
          utilizations[i] = smoothed_valid[i] ? smoothed[i] : avg_utils_[i];
        }

        uint32_t total_hosts = 0;
        for (size_t i = 0; i < locality_count; ++i) {
          weights[i] = host_counts_[i] * std::max(0.0, 1.0 - utilizations[i]);
          total_hosts += host_counts_[i];
        }

        const auto set_all_local = [&weights, &all_local]() {
          all_local = true;
          std::fill(weights.begin(), weights.end(), 0.0);
          if (!weights.empty()) {
            weights[0] = 1.0;
          }
        };

        if (all_hosts_per_locality.hasLocalLocality()) {
          double remote_util_sum = 0.0;
          uint32_t remote_hosts = 0;
          for (size_t i = 1; i < locality_count; ++i) {
            remote_util_sum += utilizations[i] * host_counts_[i];
            remote_hosts += host_counts_[i];
          }

          if (total_hosts > 0 && !weights.empty() && weights[0] > 0.0) {
            const double target_util = remote_hosts > 0 ? remote_util_sum / remote_hosts : 0.0;
            if (utilizations[0] <= target_util + utilization_variance_threshold_) {
              set_all_local();
            }
          } else if (total_hosts == 0) {
            set_all_local();
          }
        }

        if (all_hosts_per_locality.hasLocalLocality() && probe_percentage_ > 0.0 &&
            locality_count > 1) {
          double total = 0.0;
          for (double weight : weights) {
            total += weight;
          }

          if (total > 0.0) {
            const double remote_sum = total - weights[0];
            const double remote_target = total * probe_percentage_;
            if (remote_sum < remote_target) {
              uint32_t remote_hosts = 0;
              for (size_t i = 1; i < locality_count; ++i) {
                remote_hosts += host_counts_[i];
              }

              if (remote_hosts > 0) {
                const double deficit = remote_target - remote_sum;
                weights[0] = std::max(0.0, weights[0] - deficit);
                for (size_t i = 1; i < locality_count; ++i) {
                  weights[i] += deficit * static_cast<double>(host_counts_[i]) / remote_hosts;
                }
              }
            }
          }
        }

        total_weight = std::accumulate(weights.begin(), weights.end(), 0.0);
        if (total_weight == 0.0 && total_hosts > 0) {
          for (size_t i = 0; i < locality_count; ++i) {
            weights[i] = static_cast<double>(host_counts_[i]);
          }
          total_weight = static_cast<double>(total_hosts);
        }
      };

  for (size_t priority = 0; priority < host_sets.size(); ++priority) {
    const auto& host_set = host_sets[priority];
    const auto& hosts_per_locality = host_set->hostsPerLocality();
    const auto& locality_hosts = hosts_per_locality.get();
    auto& priority_snapshot = snapshot->priority_weights[priority];

    if (locality_hosts.empty()) {
      for (size_t source = 0; source < smoothed_utilizations_.size(); ++source) {
        smoothed_utilizations_[source][priority].clear();
        smoothed_utilizations_valid_[source][priority].clear();
      }
      continue;
    }

    priority_snapshot.has_local_locality = hosts_per_locality.hasLocalLocality();
    compute_source_weights(hosts_per_locality, host_set->healthyHostsPerLocality().get(),
                           priority_snapshot.weights, priority_snapshot.total_weight,
                           priority_snapshot.all_local,
                           smoothed_utilizations_[static_cast<size_t>(
                               PriorityRoutingWeights::SelectionSource::Healthy)][priority],
                           smoothed_utilizations_valid_[static_cast<size_t>(
                               PriorityRoutingWeights::SelectionSource::Healthy)][priority]);
    compute_source_weights(hosts_per_locality, host_set->degradedHostsPerLocality().get(),
                           priority_snapshot.degraded_weights,
                           priority_snapshot.degraded_total_weight,
                           priority_snapshot.degraded_all_local,
                           smoothed_utilizations_[static_cast<size_t>(
                               PriorityRoutingWeights::SelectionSource::Degraded)][priority],
                           smoothed_utilizations_valid_[static_cast<size_t>(
                               PriorityRoutingWeights::SelectionSource::Degraded)][priority]);
    compute_source_weights(hosts_per_locality, locality_hosts, priority_snapshot.all_host_weights,
                           priority_snapshot.all_host_total_weight,
                           priority_snapshot.all_host_all_local,
                           smoothed_utilizations_[static_cast<size_t>(
                               PriorityRoutingWeights::SelectionSource::AllHosts)][priority],
                           smoothed_utilizations_valid_[static_cast<size_t>(
                               PriorityRoutingWeights::SelectionSource::AllHosts)][priority]);
  }

  if (!snapshot->priority_weights.empty()) {
    const auto& priority_zero = snapshot->priority_weights[0];
    snapshot->weights = priority_zero.weights;
    snapshot->total_weight = priority_zero.total_weight;
    snapshot->all_local = priority_zero.all_local;
    snapshot->has_local_locality = priority_zero.has_local_locality;
  }

  ENVOY_LOG(trace, "computeLocalityRoutingWeights: {} priorities",
            snapshot->priority_weights.size());
  factory_->updateRoutingWeights(std::move(snapshot));
}

// --- WorkerLocalLbFactory ---

WorkerLocalLbFactory::WorkerLocalLbFactory(
    Upstream::TypedLoadBalancerFactory& child_factory, std::string child_factory_name,
    LoadBalancerConfigSharedPtr child_config, const Upstream::ClusterInfo& cluster_info,
    const Upstream::PrioritySet& cluster_priority_set, Runtime::Loader& runtime,
    Envoy::Random::RandomGenerator& random, TimeSource& time_source,
    ThreadLocal::SlotAllocator& tls_slot_allocator)
    : child_factory_name_(std::move(child_factory_name)), child_config_(std::move(child_config)),
      cluster_info_(cluster_info), random_(random) {
  auto child_config_ref =
      makeOptRefFromPtr<const Upstream::LoadBalancerConfig>(child_config_.get());
  child_thread_aware_lb_ = child_factory.create(
      child_config_ref, cluster_info_, cluster_priority_set, runtime, random_, time_source);
  tls_ = ThreadLocal::TypedSlot<ThreadLocalShim>::makeUnique(tls_slot_allocator);
  tls_->set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalShim>(); });
}

absl::Status WorkerLocalLbFactory::initializeChildLb() {
  if (child_thread_aware_lb_ == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unsupported endpoint picking policy for load_aware_locality: ", child_factory_name_,
        ". Child load balancer could not be instantiated per locality."));
  }
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
    : factory_(factory), priority_set_(priority_set), stats_(factory.lbStats()) {
  buildPerPriorityLocalities();
  // Register AFTER initial build so callback doesn't fire during construction.
  member_update_cb_ = priority_set_.addPriorityUpdateCb(
      [this](uint32_t priority, const Upstream::HostVector& hosts_added,
             const Upstream::HostVector& hosts_removed) {
        if (!hosts_added.empty() || !hosts_removed.empty()) {
          onHostChange(priority);
        } else {
          // Empty-delta updates still matter for child policies, since host attributes can change
          // in place (for example health, weight, or metadata) without host membership deltas.
          onInPlaceHostUpdate(priority);
        }
      });
}

WorkerLocalLb::~WorkerLocalLb() {
  // Reset callback handle before other members are destroyed, so the callback
  // doesn't fire during destruction and access freed per-locality state.
  member_update_cb_.reset();
}

void WorkerLocalLb::updateLocalityHosts(PerSourceLocalityState& state,
                                        const Upstream::HostVector& hosts, bool is_local,
                                        const Upstream::HostVector& hosts_added,
                                        const Upstream::HostVector& hosts_removed) {
  auto hosts_shared = std::make_shared<Upstream::HostVector>(hosts);
  auto per_locality = std::make_shared<Upstream::HostsPerLocalityImpl>(hosts, is_local);
  // All passed-in hosts are marked as "healthy" in the child priority set regardless of their
  // actual health status. The caller (syncLocalityState) already partitions hosts by source
  // (healthy/degraded/all), so this child LB only sees hosts that can be selected. Marking
  // them all healthy ensures the child policy (e.g. RoundRobin) considers every host eligible.
  auto healthy_hosts = std::make_shared<const Upstream::HealthyHostVector>(hosts);
  auto update_params = Upstream::HostSetImpl::updateHostsParams(
      hosts_shared, per_locality, healthy_hosts, per_locality,
      std::make_shared<const Upstream::DegradedHostVector>(),
      Upstream::HostsPerLocalityImpl::empty(),
      std::make_shared<const Upstream::ExcludedHostVector>(),
      Upstream::HostsPerLocalityImpl::empty());
  state.priority_set->updateHosts(0, std::move(update_params), nullptr, hosts_added, hosts_removed,
                                  absl::nullopt, absl::nullopt);
}

void WorkerLocalLb::buildPerPriorityLocalities() {
  const auto& host_sets = priority_set_.hostSetsPerPriority();
  per_priority_locality_.clear();
  per_priority_locality_.resize(host_sets.size());

  for (size_t priority = 0; priority < host_sets.size(); ++priority) {
    buildPerLocality(priority, *host_sets[priority]);
  }
}

void WorkerLocalLb::syncLocalityState(PerLocalityState& state, const Upstream::HostSet& host_set,
                                      size_t locality_index, bool recreate_child) {
  static const Upstream::HostVector empty_hosts;
  const auto& all_localities = host_set.hostsPerLocality().get();
  const auto& healthy_localities = host_set.healthyHostsPerLocality().get();
  const auto& degraded_localities = host_set.degradedHostsPerLocality().get();
  const bool is_local = (locality_index == 0 && host_set.hostsPerLocality().hasLocalLocality());

  const auto& all_hosts = all_localities[locality_index];
  const auto& healthy_hosts =
      locality_index < healthy_localities.size() ? healthy_localities[locality_index] : empty_hosts;
  const auto& degraded_hosts = locality_index < degraded_localities.size()
                                   ? degraded_localities[locality_index]
                                   : empty_hosts;

  const auto sync_source = [this, is_local, recreate_child](PerSourceLocalityState& source_state,
                                                            const Upstream::HostVector& new_hosts) {
    // Skip creating a child LB when the host list is empty.
    if (new_hosts.empty()) {
      if (source_state.priority_set != nullptr) {
        source_state.lb.reset();
        source_state.priority_set.reset();
      }
      return;
    }

    if (source_state.priority_set == nullptr) {
      source_state.priority_set = std::make_unique<Upstream::PrioritySetImpl>();
      updateLocalityHosts(source_state, new_hosts, is_local, new_hosts, {});
      source_state.lb = factory_.createWorkerChildLb(*source_state.priority_set);
      return;
    }

    const auto& old_hosts = source_state.priority_set->hostSetsPerPriority()[0]->hosts();
    absl::flat_hash_set<Upstream::HostConstSharedPtr> old_set(old_hosts.begin(), old_hosts.end());

    Upstream::HostVector hosts_added;
    Upstream::HostVector hosts_removed;
    for (const auto& host : new_hosts) {
      if (!old_set.erase(host)) {
        hosts_added.push_back(host);
      }
    }
    hosts_removed.reserve(old_set.size());
    for (const auto& host : old_set) {
      hosts_removed.push_back(std::const_pointer_cast<Upstream::Host>(host));
    }

    const bool membership_changed = !hosts_added.empty() || !hosts_removed.empty();
    if (!membership_changed) {
      // Host identity is unchanged, but child policies still need an update to observe in-place
      // host attribute changes such as weight or metadata.
      updateLocalityHosts(source_state, new_hosts, is_local, {}, {});
      return;
    }

    updateLocalityHosts(source_state, new_hosts, is_local, hosts_added, hosts_removed);
    if (recreate_child) {
      source_state.lb = factory_.createWorkerChildLb(*source_state.priority_set);
    }
  };

  sync_source(state.healthy, healthy_hosts);
  sync_source(state.degraded, degraded_hosts);
  sync_source(state.all_hosts, all_hosts);
}

void WorkerLocalLb::buildPerLocality(uint32_t priority, const Upstream::HostSet& host_set) {
  const auto& locality_hosts = host_set.hostsPerLocality().get();
  auto& per_locality = per_priority_locality_[priority].localities;
  per_locality.clear();
  per_locality.resize(locality_hosts.size());

  for (size_t i = 0; i < locality_hosts.size(); ++i) {
    syncLocalityState(per_locality[i], host_set, i, /*recreate_child=*/false);
  }
}

void WorkerLocalLb::onHostChange(uint32_t priority) {
  const auto& host_sets = priority_set_.hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return;
  }

  if (host_sets.size() != per_priority_locality_.size()) {
    buildPerPriorityLocalities();
    return;
  }

  const auto& host_set = host_sets[priority];
  const auto& locality_hosts = host_set->hostsPerLocality().get();
  auto& per_locality = per_priority_locality_[priority].localities;

  // Topology change (locality added/removed) — full rebuild.
  if (locality_hosts.size() != per_locality.size()) {
    buildPerLocality(priority, *host_set);
    return;
  }

  const bool recreate_child = factory_.recreateChildOnHostChange();
  for (size_t i = 0; i < locality_hosts.size(); ++i) {
    syncLocalityState(per_locality[i], *host_set, i, recreate_child);
  }
}

void WorkerLocalLb::onInPlaceHostUpdate(uint32_t priority) {
  const auto& host_sets = priority_set_.hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return;
  }

  if (host_sets.size() != per_priority_locality_.size()) {
    return;
  }

  const auto& host_set = host_sets[priority];
  const auto& locality_hosts = host_set->hostsPerLocality().get();
  auto& per_locality = per_priority_locality_[priority].localities;

  // Topology mismatch — wait for onHostChange to rebuild.
  if (locality_hosts.size() != per_locality.size()) {
    return;
  }

  const bool recreate_child = factory_.recreateChildOnHostChange();
  for (size_t i = 0; i < locality_hosts.size(); ++i) {
    syncLocalityState(per_locality[i], *host_set, i, recreate_child);
  }
}

Upstream::LoadBalancer*
WorkerLocalLb::pickLocalityLb(const std::vector<PerLocalityState>& per_locality,
                              PriorityRoutingWeights::SelectionSource source, size_t preferred_idx,
                              size_t& actual_idx) const {
  auto* lb = per_locality[preferred_idx].stateFor(source).lb.get();
  if (lb != nullptr) {
    actual_idx = preferred_idx;
    return lb;
  }
  // Stale routing snapshot — the preferred locality's child LB was torn down after a host
  // change but before the routing weights were recomputed. Scan for any locality with a
  // usable LB.
  for (size_t i = 0; i < per_locality.size(); ++i) {
    lb = per_locality[i].stateFor(source).lb.get();
    if (lb != nullptr) {
      actual_idx = i;
      return lb;
    }
  }
  return nullptr;
}

Upstream::HostSelectionResponse WorkerLocalLb::chooseHost(Upstream::LoadBalancerContext* context) {
  const auto selected = choosePriority();
  if (!selected.has_value()) {
    return {nullptr};
  }

  auto& per_locality = per_priority_locality_[selected->priority].localities;
  if (per_locality.empty()) {
    return {nullptr};
  }

  const size_t preferred_idx = chooseLocality(selected->priority, selected->source);
  size_t locality_idx = preferred_idx;
  auto* lb = pickLocalityLb(per_locality, selected->source, preferred_idx, locality_idx);
  if (lb == nullptr) {
    return {nullptr};
  }

  // Increment zone routing stats when a local locality is present.
  const auto* snapshot = factory_.routingWeights();
  if (snapshot != nullptr && selected->priority < snapshot->priority_weights.size()) {
    const auto& priority_snapshot = snapshot->priority_weights[selected->priority];
    if (!priority_snapshot.has_local_locality) {
      return lb->chooseHost(context);
    }

    if (locality_idx == 0) {
      if (priority_snapshot.allLocalFor(selected->source)) {
        stats_.lb_zone_routing_all_directly_.inc();
      } else {
        stats_.lb_zone_routing_sampled_.inc();
      }
    } else {
      stats_.lb_zone_routing_cross_zone_.inc();
    }
  }

  return lb->chooseHost(context);
}

absl::optional<SelectedPriority> WorkerLocalLb::firstAvailablePriority() const {
  for (size_t priority = 0; priority < per_priority_locality_.size(); ++priority) {
    if (!per_priority_locality_[priority].localities.empty()) {
      return SelectedPriority{static_cast<uint32_t>(priority),
                              PriorityRoutingWeights::SelectionSource::AllHosts};
    }
  }
  return absl::nullopt;
}

absl::optional<SelectedPriority> WorkerLocalLb::choosePriority() const {
  if (per_priority_locality_.empty()) {
    return absl::nullopt;
  }

  const auto* snapshot = factory_.routingWeights();
  if (snapshot == nullptr || snapshot->priority_weights.size() != per_priority_locality_.size() ||
      snapshot->priority_loads.healthy_priority_load_.get().size() !=
          per_priority_locality_.size() ||
      snapshot->priority_loads.degraded_priority_load_.get().size() !=
          per_priority_locality_.size()) {
    return firstAvailablePriority();
  }

  const auto healthy_total =
      std::accumulate(snapshot->priority_loads.healthy_priority_load_.get().begin(),
                      snapshot->priority_loads.healthy_priority_load_.get().end(), 0u);
  const auto degraded_total =
      std::accumulate(snapshot->priority_loads.degraded_priority_load_.get().begin(),
                      snapshot->priority_loads.degraded_priority_load_.get().end(), 0u);
  if (healthy_total + degraded_total == 0) {
    return firstAvailablePriority();
  }

  const auto priority_and_source = Upstream::LoadBalancerBase::choosePriority(
      factory_.random().random(), snapshot->priority_loads.healthy_priority_load_,
      snapshot->priority_loads.degraded_priority_load_);
  const uint32_t priority = priority_and_source.first;
  if (priority >= per_priority_locality_.size() ||
      per_priority_locality_[priority].localities.empty()) {
    return firstAvailablePriority();
  }

  SelectedPriority selected{priority, priority_and_source.second ==
                                              Upstream::LoadBalancerBase::HostAvailability::Healthy
                                          ? PriorityRoutingWeights::SelectionSource::Healthy
                                          : PriorityRoutingWeights::SelectionSource::Degraded};
  if (priority < snapshot->priority_panic.size() && snapshot->priority_panic[priority]) {
    selected.source = PriorityRoutingWeights::SelectionSource::AllHosts;
  }
  return selected;
}

size_t WorkerLocalLb::chooseLocality(uint32_t priority,
                                     PriorityRoutingWeights::SelectionSource source) const {
  const auto& per_locality = per_priority_locality_[priority].localities;
  if (per_locality.size() <= 1) {
    return 0;
  }

  const auto* snapshot = factory_.routingWeights();
  if (snapshot == nullptr || priority >= snapshot->priority_weights.size()) {
    return 0;
  }

  return selectLocality(snapshot->priority_weights[priority], source, per_locality);
}

size_t WorkerLocalLb::selectLocality(const PriorityRoutingWeights& snapshot,
                                     PriorityRoutingWeights::SelectionSource source,
                                     const std::vector<PerLocalityState>& per_locality) const {
  const auto& weights = snapshot.weightsFor(source);
  if (snapshot.totalWeightFor(source) <= 0.0 || weights.empty()) {
    return 0;
  }

  // Clamp to the valid range in case snapshot and per_locality_ have different counts
  // (transient window between a topology change and the next weight update).
  const size_t num_localities = std::min(weights.size(), per_locality.size());

  // Use the pre-computed total when counts agree. Only re-sum over the clamped range
  // during a transient topology mismatch, otherwise stale snapshot entries would spill
  // probability mass onto the last locality.
  double effective_total;
  if (num_localities == weights.size()) {
    effective_total = snapshot.totalWeightFor(source);
  } else {
    effective_total = 0.0;
    for (size_t i = 0; i < num_localities; ++i) {
      effective_total += weights[i];
    }
  }
  if (effective_total <= 0.0) {
    return 0;
  }

  auto& rng = factory_.random();
  double target = (rng.random() / static_cast<double>(rng.max())) * effective_total;
  double cumulative = 0.0;
  for (size_t i = 0; i < num_localities; ++i) {
    cumulative += weights[i];
    if (target < cumulative) {
      return i;
    }
  }

  // Floating-point rounding guard: return the last clamped locality, not an arbitrary one.
  return num_localities - 1;
}

Upstream::HostConstSharedPtr
WorkerLocalLb::peekAnotherHost(Upstream::LoadBalancerContext* context) {
  const auto selected = choosePriority();
  if (!selected.has_value()) {
    return nullptr;
  }

  auto& per_locality = per_priority_locality_[selected->priority].localities;
  if (per_locality.empty()) {
    return nullptr;
  }

  const size_t preferred_idx = chooseLocality(selected->priority, selected->source);
  size_t actual_idx = preferred_idx;
  auto* lb = pickLocalityLb(per_locality, selected->source, preferred_idx, actual_idx);
  return lb != nullptr ? lb->peekAnotherHost(context) : nullptr;
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
