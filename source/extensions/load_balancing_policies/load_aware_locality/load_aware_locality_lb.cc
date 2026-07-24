#include "source/extensions/load_balancing_policies/load_aware_locality/load_aware_locality_lb.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>

#include "envoy/stats/stats_macros.h"

#include "source/common/protobuf/utility.h"
#include "source/common/upstream/load_balancer_context_base.h"
#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"
#include "source/extensions/load_balancing_policies/common/orca_weight_manager.h"

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {

absl::Status LocalityLbHostData::onOrcaLoadReport(const Upstream::OrcaLoadReport& report,
                                                  const StreamInfo::StreamInfo&) {
  const double util =
      Common::OrcaLoadReportHandler::getUtilizationFromOrcaReport(report, *metric_names_);
  // Signal-less reports do not refresh freshness; they age out instead of pinning idle.
  if (util <= 0.0) {
    return absl::OkStatus();
  }
  storeUtilization(util, time_source_.monotonicTime());
  return absl::OkStatus();
}

// --- LoadAwareLocalityLoadBalancer (main thread) ---

LoadAwareLocalityLoadBalancer::LoadAwareLocalityLoadBalancer(
    OptRef<const Upstream::LoadBalancerConfig> lb_config, const Upstream::ClusterInfo& cluster_info,
    const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
    Envoy::Random::RandomGenerator& random, TimeSource& time_source)
    : priority_set_(priority_set), lb_stats_{ALL_LOAD_AWARE_LOCALITY_STATS(POOL_COUNTER_PREFIX(
                                       cluster_info.statsScope(), "load_aware_locality"))},
      time_source_(time_source) {
  const auto* typed_config = dynamic_cast<const LoadAwareLocalityLbConfig*>(lb_config.ptr());
  ASSERT(typed_config != nullptr);

  utilization_variance_threshold_ = typed_config->utilizationVarianceThreshold();
  ewma_alpha_ = typed_config->ewmaAlpha();
  remote_probe_fraction_ = typed_config->remoteProbeFraction();
  weight_expiration_period_ = typed_config->weightExpirationPeriod();
  weight_update_period_ = typed_config->weightUpdatePeriod();
  metric_names_ = std::make_shared<const std::vector<std::string>>(
      typed_config->metricNamesForComputingUtilization());

  child_factory_name_ = typed_config->endpointPickingPolicyName();
  auto child_config_ref = makeOptRefFromPtr<const Upstream::LoadBalancerConfig>(
      typed_config->endpointPickingPolicyConfig().get());
  child_thread_aware_lb_ = typed_config->endpointPickingPolicyFactory().create(
      child_config_ref, cluster_info, priority_set, runtime, random, time_source);

  factory_ = std::make_shared<WorkerLocalLbFactory>(
      child_thread_aware_lb_ != nullptr ? child_thread_aware_lb_->factory() : nullptr,
      typed_config->endpointPickingPolicyConfig(), cluster_info, runtime, random,
      typed_config->tlsSlotAllocator());

  weight_update_timer_ = typed_config->mainThreadDispatcher().createTimer(
      [this]() { computeLocalityRoutingWeights(); });
}

LoadAwareLocalityLoadBalancer::~LoadAwareLocalityLoadBalancer() = default;

void LoadAwareLocalityLoadBalancer::addLbPolicyDataToHosts(const Upstream::HostVector& hosts) {
  for (const auto& host_ptr : hosts) {
    if (!host_ptr->typedLbPolicyData<LocalityLbHostData>().has_value()) {
      auto data = std::make_unique<LocalityLbHostData>(time_source_, metric_names_);
      host_ptr->addLbPolicyData(std::move(data));
    }
  }
}

absl::Status LoadAwareLocalityLoadBalancer::initialize() {
  if (child_thread_aware_lb_ == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Unsupported endpoint picking policy for load_aware_locality: ", child_factory_name_,
        ". Child load balancer could not be instantiated per locality."));
  }
  RETURN_IF_NOT_OK(child_thread_aware_lb_->initialize());

  for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
    addLbPolicyDataToHosts(host_set->hosts());
  }

  priority_update_cb_ = priority_set_.addPriorityUpdateCb(
      [this](uint32_t, const Upstream::HostVector& hosts_added, const Upstream::HostVector&) {
        addLbPolicyDataToHosts(hosts_added);
      });

  computeLocalityRoutingWeights();
  return absl::OkStatus();
}

void LoadAwareLocalityLoadBalancer::computeLocalityRoutingWeights() {
  weight_update_timer_->enableTimer(weight_update_period_);

  auto snapshot = std::make_shared<RoutingWeightsSnapshot>();
  const auto& host_sets = priority_set_.hostSetsPerPriority();
  snapshot->priority_weights.resize(host_sets.size());

  for (auto& per_source_state : ewma_state_) {
    per_source_state.resize(host_sets.size());
  }

  const MonotonicTime now = time_source_.monotonicTime();

  bool tick_all_overloaded = false;
  bool tick_local_preferred = false;
  bool tick_probe_active = false;
  bool tick_spill_active = false;
  uint32_t tick_stale_localities = 0;

  for (size_t priority = 0; priority < host_sets.size(); ++priority) {
    const auto& host_set = host_sets[priority];
    const auto& hosts_per_locality = host_set->hostsPerLocality();
    const auto& locality_hosts = hosts_per_locality.get();
    auto& priority_snapshot = snapshot->priority_weights[priority];

    if (locality_hosts.empty()) {
      for (auto& per_source_state : ewma_state_) {
        per_source_state[priority].clear();
      }
      continue;
    }

    using SelectionSource = PriorityRoutingWeights::SelectionSource;
    const auto run_source = [&](SelectionSource src,
                                const std::vector<Upstream::HostVector>& eligible_hosts) {
      const size_t s = static_cast<size_t>(src);
      auto& sw = priority_snapshot.by_source[s];
      return computeSourceWeights(hosts_per_locality, eligible_hosts, now, sw.weights, sw.all_local,
                                  ewma_state_[s][priority]);
    };
    const auto healthy_result =
        run_source(SelectionSource::Healthy, host_set->healthyHostsPerLocality().get());
    const auto degraded_result =
        run_source(SelectionSource::Degraded, host_set->degradedHostsPerLocality().get());
    const auto all_hosts_result = run_source(SelectionSource::AllHosts, locality_hosts);

    tick_all_overloaded |= healthy_result.all_overloaded || degraded_result.all_overloaded ||
                           all_hosts_result.all_overloaded;
    tick_local_preferred |= healthy_result.local_preferred || degraded_result.local_preferred ||
                            all_hosts_result.local_preferred;
    tick_probe_active |= healthy_result.probe_active || degraded_result.probe_active ||
                         all_hosts_result.probe_active;
    tick_spill_active |= healthy_result.spill_active || degraded_result.spill_active ||
                         all_hosts_result.spill_active;
    tick_stale_localities += all_hosts_result.stale_localities;
  }

  if (tick_all_overloaded) {
    lb_stats_.all_overloaded_total_.inc();
  }
  if (tick_local_preferred) {
    lb_stats_.local_preferred_total_.inc();
  }
  if (tick_probe_active) {
    lb_stats_.probe_active_total_.inc();
  }
  if (tick_spill_active) {
    lb_stats_.spill_active_total_.inc();
  }
  if (tick_stale_localities > 0) {
    lb_stats_.stale_locality_total_.add(tick_stale_localities);
  }
  lb_stats_.recompute_total_.inc();

  ENVOY_LOG(trace, "computeLocalityRoutingWeights: {} priorities",
            snapshot->priority_weights.size());
  factory_->updateRoutingWeights(std::move(snapshot));
}

LoadAwareLocalityLoadBalancer::SourceComputeResult
LoadAwareLocalityLoadBalancer::computeSourceWeights(
    const Upstream::HostsPerLocality& all_hosts_per_locality,
    const std::vector<Upstream::HostVector>& eligible_hosts_per_locality, MonotonicTime now,
    LocalityRoutingWeightsMap& weights_map, bool& all_local, LocalityEwmaMap& ewma_state) {
  SourceComputeResult result;
  const auto& locality_hosts = all_hosts_per_locality.get();
  const size_t locality_count = locality_hosts.size();
  std::vector<double> weights(locality_count, 0.0);
  all_local = false;

  std::vector<double> avg_utils(locality_count, 0.0);
  std::vector<uint32_t> valid_counts(locality_count, 0);
  std::vector<uint32_t> host_counts(locality_count, 0);

  for (size_t i = 0; i < locality_count; ++i) {
    const bool has_eligible = i < eligible_hosts_per_locality.size();
    host_counts[i] =
        has_eligible ? static_cast<uint32_t>(eligible_hosts_per_locality[i].size()) : 0u;

    double util_sum = 0.0;
    uint32_t valid_count = 0;
    if (has_eligible) {
      for (const auto& host : eligible_hosts_per_locality[i]) {
        auto host_data = host->typedLbPolicyData<LocalityLbHostData>();
        if (!host_data.has_value()) {
          continue;
        }
        const MonotonicTime last_update = host_data->lastUpdateTime();
        if (last_update == LocalityLbHostData::kNeverReported) {
          continue;
        }

        if (weight_expiration_period_.count() > 0 &&
            (now - last_update) > weight_expiration_period_) {
          continue;
        }

        util_sum += host_data->utilization();
        valid_count++;
      }
    }

    avg_utils[i] = valid_count > 0 ? util_sum / valid_count : 0.0;
    valid_counts[i] = valid_count;
  }

  LocalityEwmaMap new_state;
  new_state.reserve(locality_count);
  std::vector<double> utilizations(locality_count, 0.0);
  std::vector<bool> stale(locality_count, false);
  for (size_t i = 0; i < locality_count; ++i) {
    if (locality_hosts[i].empty()) {
      continue; // No identity and no hosts: weight below is 0 regardless.
    }
    const auto prev = ewma_state.find(locality_hosts[i][0]->locality());
    if (valid_counts[i] > 0) {
      const double smoothed = prev == ewma_state.end()
                                  ? avg_utils[i]
                                  : ewma_alpha_ * avg_utils[i] + (1.0 - ewma_alpha_) * prev->second;
      new_state[locality_hosts[i][0]->locality()] = smoothed;
      utilizations[i] = smoothed;
    } else if (prev != ewma_state.end()) {
      new_state[locality_hosts[i][0]->locality()] = prev->second;
      utilizations[i] = prev->second;
      stale[i] = true;
      ++result.stale_localities;
    }
  }
  ewma_state = std::move(new_state);

  uint32_t total_hosts = 0;
  for (size_t i = 0; i < locality_count; ++i) {
    weights[i] = stale[i] ? static_cast<double>(host_counts[i])
                          : host_counts[i] * std::max(0.0, 1.0 - utilizations[i]);
    total_hosts += host_counts[i];
  }

  const auto set_all_local = [&weights, &all_local]() {
    all_local = true;
    std::fill(weights.begin(), weights.end(), 0.0);
    weights[0] = 1.0;
  };

  const double total_base_weight = std::accumulate(weights.begin(), weights.end(), 0.0);
  if (total_base_weight == 0.0 && total_hosts > 0) {
    for (size_t i = 0; i < locality_count; ++i) {
      weights[i] = static_cast<double>(host_counts[i]);
    }
    result.all_overloaded = true;
  } else if (total_base_weight > 0.0) {
    uint32_t remote_hosts = 0;
    if (all_hosts_per_locality.hasLocalLocality()) {
      double remote_util_sum = 0.0;
      for (size_t i = 1; i < locality_count; ++i) {
        remote_util_sum += utilizations[i] * host_counts[i];
        remote_hosts += host_counts[i];
      }

      // Comparison needs eligible local hosts: an empty local slice spills for health, not load,
      // and does not count as spill.
      if (host_counts[0] > 0 && remote_hosts > 0) {
        const double target_util = remote_util_sum / remote_hosts;
        if (utilizations[0] <= target_util + utilization_variance_threshold_) {
          set_all_local();
          result.local_preferred = true;
        } else {
          result.spill_active = true;
        }
      }
    }

    if (remote_hosts > 0 && remote_probe_fraction_ > 0.0) {
      const double total = std::accumulate(weights.begin(), weights.end(), 0.0);
      const double remote_target = total * remote_probe_fraction_;
      const double remote_sum = total - weights[0];
      if (remote_sum < remote_target) {
        const double take_from_local = std::min(remote_target - remote_sum, weights[0]);
        weights[0] -= take_from_local;
        for (size_t i = 1; i < locality_count; ++i) {
          weights[i] += take_from_local * static_cast<double>(host_counts[i]) / remote_hosts;
        }
        result.probe_active = true;
        // Weights are no longer 100% local, so local picks count as sampled, not all-directly.
        all_local = false;
      }
    }
  }

  weights_map.clear();
  weights_map.reserve(locality_count);
  for (size_t i = 0; i < locality_count; ++i) {
    if (locality_hosts[i].empty()) {
      continue;
    }
    weights_map[locality_hosts[i][0]->locality()] = weights[i];
  }

  return result;
}

// --- WorkerLocalLbFactory ---

WorkerLocalLbFactory::WorkerLocalLbFactory(
    Upstream::LoadBalancerFactorySharedPtr child_worker_factory,
    LoadBalancerConfigSharedPtr child_config, const Upstream::ClusterInfo& cluster_info,
    Runtime::Loader& runtime, Envoy::Random::RandomGenerator& random,
    ThreadLocal::SlotAllocator& tls_slot_allocator)
    : child_worker_factory_(std::move(child_worker_factory)),
      child_config_(std::move(child_config)), cluster_info_(cluster_info), random_(random),
      runtime_(runtime), healthy_panic_threshold_(PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(
                             cluster_info.lbConfig(), healthy_panic_threshold, 100, 50)) {
  tls_ = ThreadLocal::TypedSlot<ThreadLocalShim>::makeUnique(tls_slot_allocator);
  tls_->set([](Event::Dispatcher&) { return std::make_shared<ThreadLocalShim>(); });
}

Upstream::LoadBalancerPtr
WorkerLocalLbFactory::createWorkerChildLb(Upstream::PrioritySetImpl& per_locality_priority_set) {
  // Non-null is guaranteed: the main-thread LB fails initialize() when the child policy could not
  // be instantiated, so the cluster never reaches workers.
  ASSERT(child_worker_factory_ != nullptr);
  Upstream::LoadBalancerParams child_params{per_locality_priority_set, nullptr};
  return child_worker_factory_->create(child_params);
}

bool WorkerLocalLbFactory::recreateChildOnHostChange() const {
  ASSERT(child_worker_factory_ != nullptr);
  return child_worker_factory_->recreateOnHostChangeDeprecated();
}

Upstream::LoadBalancerPtr WorkerLocalLbFactory::create(Upstream::LoadBalancerParams params) {
  return std::make_unique<WorkerLocalLb>(*this, params.priority_set);
}

namespace {

// Context for per-locality child LBs. The base's non-delegating determinePriorityLoad keeps
// priority selection fixed to the child's default: the parent already ran retry-priority against
// cluster-wide priorities, and rerunning it against the single-priority child set can corrupt
// plugin state. The base's no-op onAsyncHostSelection matches chooseHost cancelling async child
// selection. Other methods pass through; children must not retain this stack object.
class ChildLoadBalancerContext : public Upstream::LoadBalancerContextBase {
public:
  explicit ChildLoadBalancerContext(Upstream::LoadBalancerContext& wrapped) : wrapped_(wrapped) {}

  std::optional<uint64_t> computeHashKey() override { return wrapped_.computeHashKey(); }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() override {
    return wrapped_.metadataMatchCriteria();
  }
  const Network::Connection* downstreamConnection() const override {
    return wrapped_.downstreamConnection();
  }
  StreamInfo::StreamInfo* requestStreamInfo() const override {
    return wrapped_.requestStreamInfo();
  }
  const Http::RequestHeaderMap* downstreamHeaders() const override {
    return wrapped_.downstreamHeaders();
  }
  bool shouldSelectAnotherHost(const Upstream::Host& host) override {
    return wrapped_.shouldSelectAnotherHost(host);
  }
  uint32_t hostSelectionRetryCount() const override { return wrapped_.hostSelectionRetryCount(); }
  Network::Socket::OptionsSharedPtr upstreamSocketOptions() const override {
    return wrapped_.upstreamSocketOptions();
  }
  Network::TransportSocketOptionsConstSharedPtr upstreamTransportSocketOptions() const override {
    return wrapped_.upstreamTransportSocketOptions();
  }
  OptRef<const Upstream::LoadBalancerContext::OverrideHost> overrideHostToSelect() const override {
    return wrapped_.overrideHostToSelect();
  }
  void setHeadersModifier(std::function<void(Http::ResponseHeaderMap&)> modifier) override {
    wrapped_.setHeadersModifier(std::move(modifier));
  }

private:
  Upstream::LoadBalancerContext& wrapped_;
};

// splitMix64 (golden-ratio increment + finishing mix) makes the locality target independent of
// choosePriority's hash % 100 so both decisions can share the pick's single random draw.
uint64_t splitMix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
  x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
  return x ^ (x >> 31);
}

// Async child selection would retain ChildLoadBalancerContext after chooseHost returns. Cancel any
// async handle and fail synchronously, matching the subset LB.
Upstream::HostSelectionResponse failOnAsyncSelection(Upstream::HostSelectionResponse response) {
  if (response.cancelable != nullptr) {
    response.cancelable->cancel();
    return {nullptr};
  }
  return response;
}

} // namespace

// --- WorkerLocalLb (per-worker) ---

WorkerLocalLb::WorkerLocalLb(WorkerLocalLbFactory& factory,
                             const Upstream::PrioritySet& priority_set)
    : Upstream::LoadBalancerBase(priority_set, factory.lbStats(), factory.runtime(),
                                 factory.random(), factory.healthyPanicThreshold()),
      factory_(factory) {
  buildPerPriorityLocalities();
  // Register AFTER initial build so callback doesn't fire during construction.
  priority_sync_cb_ = priority_set_.addPriorityUpdateCb(
      [this](uint32_t priority, const Upstream::HostVector& hosts_added,
             const Upstream::HostVector& hosts_removed) {
        // Empty-delta updates refresh child host attributes without rebuilding topology.
        syncPriority(priority, /*allow_rebuild=*/!hosts_added.empty() || !hosts_removed.empty());
      });
}

WorkerLocalLb::~WorkerLocalLb() { priority_sync_cb_.reset(); }

void WorkerLocalLb::updateLocalityHosts(PerSourceLocalityState& state,
                                        const Upstream::HostVector& hosts, bool is_local,
                                        const Upstream::HostVector& hosts_added,
                                        const Upstream::HostVector& hosts_removed) {
  auto hosts_shared = std::make_shared<Upstream::HostVector>(hosts);
  auto per_locality = std::make_shared<Upstream::HostsPerLocalityImpl>(hosts, is_local);
  // The caller has already partitioned by source, so every host passed to the child is eligible.
  auto healthy_hosts = std::make_shared<const Upstream::HealthyHostVector>(hosts);
  auto update_params = Upstream::HostSetImpl::updateHostsParams(
      hosts_shared, per_locality, healthy_hosts, per_locality,
      std::make_shared<const Upstream::DegradedHostVector>(),
      Upstream::HostsPerLocalityImpl::empty(),
      std::make_shared<const Upstream::ExcludedHostVector>(),
      Upstream::HostsPerLocalityImpl::empty());
  state.priority_set->updateHosts(0, std::move(update_params), nullptr, hosts_added, hosts_removed,
                                  std::nullopt, std::nullopt);
}

void WorkerLocalLb::buildPerPriorityLocalities() {
  const auto& host_sets = priority_set_.hostSetsPerPriority();
  // Resize preserves existing priorities' child LBs; build only the newly added ones.
  const size_t old_size = per_priority_locality_.size();
  per_priority_locality_.resize(host_sets.size());

  for (size_t priority = old_size; priority < host_sets.size(); ++priority) {
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

  // Record this locality's identity for chooseLocality's weight lookup. Empty group has none.
  state.locality =
      all_hosts.empty() ? envoy::config::core::v3::Locality() : all_hosts[0]->locality();

  const auto sync_source = [this, is_local, recreate_child](PerSourceLocalityState& source_state,
                                                            const Upstream::HostVector& new_hosts) {
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
    absl::flat_hash_set<const Upstream::Host*> old_set;
    old_set.reserve(old_hosts.size());
    for (const auto& host : old_hosts) {
      old_set.insert(host.get());
    }

    Upstream::HostVector hosts_added;
    Upstream::HostVector hosts_removed;
    for (const auto& host : new_hosts) {
      if (!old_set.erase(host.get())) {
        hosts_added.push_back(host);
      }
    }
    for (const auto& host : old_hosts) {
      if (old_set.contains(host.get())) {
        hosts_removed.push_back(host);
      }
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

  sync_source(state.stateFor(PriorityRoutingWeights::SelectionSource::Healthy), healthy_hosts);
  sync_source(state.stateFor(PriorityRoutingWeights::SelectionSource::Degraded), degraded_hosts);
  sync_source(state.stateFor(PriorityRoutingWeights::SelectionSource::AllHosts), all_hosts);
}

void WorkerLocalLb::buildPerLocality(uint32_t priority, const Upstream::HostSet& host_set) {
  // Locality routing structures for this priority are (re)generated on this worker, the same
  // event the zone-aware counter tracks.
  stats_.lb_recalculate_zone_structures_.inc();
  // Topology changed: force refreshLocalityWeights to rebuild the index-aligned weights next pick.
  built_snapshot_.reset();
  const auto& locality_hosts = host_set.hostsPerLocality().get();
  per_priority_locality_[priority].has_local_locality =
      host_set.hostsPerLocality().hasLocalLocality();
  auto& per_locality = per_priority_locality_[priority].localities;
  per_locality.clear();
  per_locality.resize(locality_hosts.size());

  for (size_t i = 0; i < locality_hosts.size(); ++i) {
    syncLocalityState(per_locality[i], host_set, i, /*recreate_child=*/false);
  }
}

void WorkerLocalLb::syncPriority(uint32_t priority, bool allow_rebuild) {
  const auto& host_sets = priority_set_.hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return;
  }

  // Priority count changed; build the newly added priorities when permitted.
  if (host_sets.size() != per_priority_locality_.size()) {
    const size_t built = per_priority_locality_.size();
    if (allow_rebuild) {
      buildPerPriorityLocalities();
    }
    if (priority >= built) {
      return; // Newly built or still-pending priority; the build read current membership.
    }
    // An existing priority triggered the callback; fall through to apply its update.
  }

  const auto& host_set = host_sets[priority];
  const auto& locality_hosts = host_set->hostsPerLocality().get();
  auto& per_locality = per_priority_locality_[priority].localities;

  // Topology changed; rebuild this priority when permitted, else wait.
  if (locality_hosts.size() != per_locality.size()) {
    if (allow_rebuild) {
      buildPerLocality(priority, *host_set);
    }
    return;
  }

  per_priority_locality_[priority].has_local_locality =
      host_set->hostsPerLocality().hasLocalLocality();
  const bool recreate_child = factory_.recreateChildOnHostChange();
  for (size_t i = 0; i < locality_hosts.size(); ++i) {
    syncLocalityState(per_locality[i], *host_set, i, recreate_child);
  }
  // A membership delta can flip a locality's identity (e.g. draining to empty), so rebuild the
  // index-aligned weights on the next pick.
  if (allow_rebuild) {
    built_snapshot_.reset();
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
  // Stale routing snapshot: the preferred locality's child LB was torn down after a host
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

WorkerLocalLb::PrioritySourcePick
WorkerLocalLb::resolvePrioritySource(Upstream::LoadBalancerContext* context, bool peeking) {
  using SelectionSource = PriorityRoutingWeights::SelectionSource;
  if (priority_set_.hostSetsPerPriority().empty()) {
    // per_priority_locality_ is empty too, so selectLocalityLb's bounds guard yields no selection.
    return {0, SelectionSource::Healthy, 0, /*in_panic=*/false};
  }

  const uint64_t hash = random(peeking);

  // Live priority/health selection from worker-local LoadBalancerBase state.
  const auto host_set_and_availability = chooseHostSet(context, hash);
  const uint32_t priority = host_set_and_availability.first.priority();

  if (isInPanic(priority)) {
    return {priority, SelectionSource::AllHosts, hash, /*in_panic=*/true};
  }
  const SelectionSource source =
      host_set_and_availability.second == Upstream::LoadBalancerBase::HostAvailability::Healthy
          ? SelectionSource::Healthy
          : SelectionSource::Degraded;
  return {priority, source, hash, /*in_panic=*/false};
}

WorkerLocalLb::LocalityLbSelection WorkerLocalLb::selectLocalityLb(const PrioritySourcePick& pick) {
  LocalityLbSelection selection;
  // Covers the empty priority set and a custom retry-priority plugin routing to a just-created
  // priority that per_priority_locality_ doesn't cover yet (empty-delta update, rebuild pending).
  if (pick.priority >= per_priority_locality_.size()) {
    return selection;
  }
  auto& per_locality = per_priority_locality_[pick.priority].localities;
  if (per_locality.empty()) {
    return selection;
  }

  if (shim_ == nullptr) {
    shim_ = factory_.tlsShim();
  }
  const RoutingWeightsSnapshot* snapshot =
      shim_ != nullptr ? shim_->routing_weights.get() : nullptr;
  selection.snapshot = snapshot;
  refreshLocalityWeights(snapshot);
  const size_t preferred_idx = chooseLocality(pick.priority, pick.source, pick.hash);
  selection.lb = pickLocalityLb(per_locality, pick.source, preferred_idx, selection.locality_idx);
  return selection;
}

void WorkerLocalLb::recordZoneRoutingStats(const PrioritySourcePick& pick,
                                           const LocalityLbSelection& selection) {
  if (pick.in_panic || selection.snapshot == nullptr ||
      pick.priority >= selection.snapshot->priority_weights.size()) {
    return;
  }

  // "Is index 0 local" is worker-live state (the snapshot may lag topology); the snapshot,
  // whose coverage is gated above, only supplies the all-local flag for the split below.
  if (!per_priority_locality_[pick.priority].has_local_locality) {
    return;
  }
  if (selection.locality_idx != 0) {
    stats_.lb_zone_routing_cross_zone_.inc();
    return;
  }
  if (selection.snapshot->priority_weights[pick.priority].allLocalFor(pick.source)) {
    stats_.lb_zone_routing_all_directly_.inc();
  } else {
    stats_.lb_zone_routing_sampled_.inc();
  }
}

Upstream::HostSelectionResponse WorkerLocalLb::chooseHost(Upstream::LoadBalancerContext* context) {
  const auto pick = resolvePrioritySource(context, /*peeking=*/false);
  // Panic stat is counted here only; peekAnotherHost deliberately does not double-count it.
  if (pick.in_panic) {
    stats_.lb_healthy_panic_.inc();
  }

  const auto selection = selectLocalityLb(pick);
  if (selection.lb == nullptr) {
    return {nullptr};
  }

  recordZoneRoutingStats(pick, selection);

  if (context == nullptr) {
    return failOnAsyncSelection(selection.lb->chooseHost(nullptr));
  }
  ChildLoadBalancerContext child_context(*context);
  return failOnAsyncSelection(selection.lb->chooseHost(&child_context));
}

void WorkerLocalLb::refreshLocalityWeights(const RoutingWeightsSnapshot* snapshot) {
  if (snapshot == built_snapshot_.get()) {
    return; // Already built for this snapshot; a topology change reset built_snapshot_.
  }
  // Cold path: take shared ownership so built_snapshot_ pins this snapshot's address and the raw
  // compare above stays ABA-free.
  built_snapshot_ = shim_->routing_weights;
  ASSERT(built_snapshot_.get() == snapshot);

  for (size_t priority = 0; priority < per_priority_locality_.size(); ++priority) {
    auto& pstate = per_priority_locality_[priority];
    const size_t locality_count = pstate.localities.size();
    const bool have_priority = snapshot != nullptr && priority < snapshot->priority_weights.size();

    for (size_t s = 0; s < pstate.source_weights.size(); ++s) {
      auto& index_weights = pstate.source_weights[s];
      index_weights.assign(locality_count, 0.0);
      double total = 0.0;
      if (have_priority) {
        // Once-per-publish replacement for chooseLocality's old per-pick weights.find(locality),
        // stored as prefix sums so the pick is a binary search. Snapshot-missing live localities
        // get weight 0 until the next recompute.
        const auto& weights = snapshot->priority_weights[priority].weightsFor(
            static_cast<PriorityRoutingWeights::SelectionSource>(s));
        for (size_t i = 0; i < locality_count; ++i) {
          const auto weight = weights.find(pstate.localities[i].locality);
          total += weight != weights.end() ? weight->second : 0.0;
          index_weights[i] = total;
        }
      }
    }
  }
}

size_t WorkerLocalLb::chooseLocality(uint32_t priority,
                                     PriorityRoutingWeights::SelectionSource source,
                                     uint64_t hash) {
  const auto& pstate = per_priority_locality_[priority];
  const auto& per_locality = pstate.localities;
  if (per_locality.size() <= 1) {
    return 0;
  }
  const size_t s = static_cast<size_t>(source);
  const auto& cumulative_weights = pstate.source_weights[s];
  if (cumulative_weights.size() != per_locality.size()) {
    return 0;
  }
  // Zero total: no snapshot yet, the snapshot lags this worker's membership (every identity
  // lookup missed), or the published weights for this priority are all zero.
  const double effective_total = cumulative_weights.back();
  if (effective_total <= 0.0) {
    return 0;
  }

  const double target = (static_cast<double>(splitMix64(hash)) /
                         static_cast<double>(std::numeric_limits<uint64_t>::max())) *
                        effective_total;
  const auto it = std::upper_bound(cumulative_weights.begin(), cumulative_weights.end(), target);
  // Floating-point rounding guard: clamp to the last locality, not an arbitrary one.
  return std::min<size_t>(static_cast<size_t>(it - cumulative_weights.begin()),
                          per_locality.size() - 1);
}

Upstream::HostConstSharedPtr
WorkerLocalLb::peekAnotherHost(Upstream::LoadBalancerContext* context) {
  // Cap preconnect look-ahead as the sibling LBs do; each peek stashes exactly one draw, so the
  // stash size is the number of outstanding peeked picks.
  if (Upstream::tooManyPreconnects(stashed_random_.size(), total_healthy_hosts_)) {
    return nullptr;
  }

  const auto selection = selectLocalityLb(resolvePrioritySource(context, /*peeking=*/true));
  if (selection.lb == nullptr) {
    return nullptr;
  }
  if (context == nullptr) {
    return selection.lb->peekAnotherHost(nullptr);
  }
  ChildLoadBalancerContext child_context(*context);
  return selection.lb->peekAnotherHost(&child_context);
}

} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
