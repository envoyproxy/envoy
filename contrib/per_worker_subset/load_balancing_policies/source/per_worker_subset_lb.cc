#include "contrib/per_worker_subset/load_balancing_policies/source/per_worker_subset_lb.h"

#include <algorithm>
#include <vector>

#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"

#include "source/extensions/load_balancing_policies/least_request/least_request_lb.h"
#include "source/extensions/load_balancing_policies/round_robin/round_robin_lb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PerWorkerSubset {

PerWorkerSubsetLoadBalancer::PerWorkerSubsetLoadBalancer(
    const Upstream::PrioritySet& priority_set, Upstream::ClusterLbStats& stats, Stats::Scope& scope,
    Runtime::Loader& runtime, ::Envoy::Random::RandomGenerator& random, TimeSource& time_source,
    uint32_t subset_size, PartitioningStrategy strategy,
    HostSelectionStrategy host_selection_strategy, uint32_t worker_id, uint32_t total_workers,
    uint32_t fallback_threshold, uint64_t envoy_seed,
    const envoy::extensions::load_balancing_policies::common::v3::SlowStartConfig&
        slow_start_config)
    : priority_set_(priority_set), stats_(stats),
      per_worker_subset_stats_{ALL_PER_WORKER_SUBSET_STATS(POOL_COUNTER_PREFIX(scope, ""))},
      runtime_(runtime), random_(random), time_source_(time_source), subset_size_(subset_size),
      strategy_(strategy), host_selection_strategy_(host_selection_strategy), worker_id_(worker_id),
      total_workers_(total_workers), fallback_threshold_(fallback_threshold),
      envoy_seed_(envoy_seed),
      subset_(std::make_shared<std::vector<Upstream::HostConstSharedPtr>>()) {
  // Seed priority 0 on the synthetic priority set BEFORE constructing the
  // inner LB. The stock LB base's constructor calls
  // ``recalculatePerPriorityPanic()`` -> ``recalculateLoadInTotalPanic()``,
  // which dereferences ``per_priority_load_.healthy_priority_load_.get()[0]``.
  // That vector is sized from ``hostSetsPerPriority()``, so without a
  // host_set at priority 0 it is empty and the access segfaults. The inner
  // stock LBs are then constructed against ``synthetic_priority_set_``,
  // which has a single empty host_set; they install their own
  // priority-update callbacks during construction. Subsequent
  // ``rebuildSubset()`` calls then push the latest K hosts and trigger
  // those callbacks.
  if (host_selection_strategy_ == HostSelectionStrategy::EnvoyRoundRobin ||
      host_selection_strategy_ == HostSelectionStrategy::EnvoyP2C) {
    synthetic_priority_set_.getOrCreateHostSet(0);
  }
  switch (host_selection_strategy_) {
  case HostSelectionStrategy::EnvoyRoundRobin: {
    envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin rr_config;
    // Copy slow_start_config from our extension proto into the inner
    // RoundRobin proto. Stock RR's EdfLoadBalancerBase reads it on init.
    // Unset = empty proto = slow_start disabled (default behavior).
    *rr_config.mutable_slow_start_config() = slow_start_config;
    inner_lb_ = std::make_unique<Upstream::RoundRobinLoadBalancer>(
        synthetic_priority_set_, /*local_priority_set=*/nullptr, stats_, runtime_, random_,
        fallback_threshold_, rr_config, time_source_);
    break;
  }
  case HostSelectionStrategy::EnvoyP2C: {
    envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest lr_config;
    *lr_config.mutable_slow_start_config() = slow_start_config;
    inner_lb_ = std::make_unique<Upstream::LeastRequestLoadBalancer>(
        synthetic_priority_set_, /*local_priority_set=*/nullptr, stats_, runtime_, random_,
        fallback_threshold_, lr_config, time_source_);
    break;
  }
  case HostSelectionStrategy::SimpleRoundRobin:
    break;
  case HostSelectionStrategy::Unspecified:
    // The factory's ``loadConfig`` rejects ``Unspecified``; reaching here in
    // production is impossible. Treated as ``SimpleRoundRobin`` defensively
    // (no inner LB) so the LB still functions if a test plumbs it directly.
    break;
  }

  rebuildSubset(/*membership_changed=*/true);
  // ``PriorityUpdateCb`` (not ``MemberUpdateCb``) -- fires on every
  // priority-set update including health-flag transitions that do not move
  // hosts in/out of membership. This matches what stock LBs (RoundRobin,
  // LeastRequest) hook into so the subset stays in lock-step with health
  // changes from active health checks and outlier detection, not just EDS
  // adds/removes.
  member_update_cb_ = priority_set_.addPriorityUpdateCb(
      [this](uint32_t /*priority*/, const Upstream::HostVector& hosts_added,
             const Upstream::HostVector& hosts_removed) {
        rebuildSubset(/*membership_changed=*/!hosts_added.empty() || !hosts_removed.empty());
        return absl::OkStatus();
      });
}

void PerWorkerSubsetLoadBalancer::rebuildSubset(bool membership_changed) {
  per_worker_subset_stats_.lb_per_worker_subset_rebuilds_.inc();
  if (priority_set_.hostSetsPerPriority().empty()) {
    subset_ = std::make_shared<std::vector<Upstream::HostConstSharedPtr>>();
    publishSubsetToSyntheticPrioritySet(*subset_);
    return;
  }
  const auto& host_set = priority_set_.hostSetsPerPriority()[0];
  const auto& all = host_set->hosts();

  auto new_subset = std::make_shared<std::vector<Upstream::HostConstSharedPtr>>();
  if (all.empty()) {
    if (membership_changed) {
      equal_partition_.clear();
      random_partition_.clear();
    }
    subset_ = std::move(new_subset);
    publishSubsetToSyntheticPrioritySet(*subset_);
    return;
  }

  // Per-worker fallback -- computed inside the rebuild helpers against this
  // worker's own slice, not against cluster-wide healthy fraction. This is
  // the key difference from stock LB cluster-wide fallback: a brief deploy
  // that flips 50% of hosts unhealthy no longer trips every worker on this
  // Envoy simultaneously; only workers whose K-host slice happens to overlap
  // a heavily-unhealthy band fall back. Other workers keep their stable
  // healthy-only subset, avoiding the synchronized connection-pool churn
  // that a cluster-wide check would produce.
  std::vector<Upstream::HostConstSharedPtr> picked;
  if (strategy_ == PartitioningStrategy::EqualPartitions) {
    if (membership_changed) {
      rebuildEqualPartitionAssignment(all);
    }
    rebuildEqualPartition(picked);
  } else {
    if (membership_changed) {
      reconcileRandomPartition(host_set->healthyHosts(), all);
    }
    rebuildRandomPartition(picked);
  }

  ENVOY_LOG(debug, "per_worker_subset: worker={} strategy={} intra={} K={} from N={}", worker_id_,
            static_cast<int>(strategy_), static_cast<int>(host_selection_strategy_), picked.size(),
            all.size());

  new_subset->swap(picked);
  subset_ = std::move(new_subset);
  publishSubsetToSyntheticPrioritySet(*subset_);
}

void PerWorkerSubsetLoadBalancer::reconcileRandomPartition(
    const Upstream::HostVector& healthy_candidates, const Upstream::HostVector& all_candidates) {
  // ``healthy_candidates`` and ``all_candidates`` are read-only views of the
  // worker's current priority-0 cluster-wide host lists; this function does
  // not create or modify global host state. It uses those lists to reconcile
  // this worker's persistent random assignment, retaining assigned hosts that
  // still exist and filling only membership-created vacancies.
  // ``rebuildRandomPartition()`` subsequently derives the worker's effective
  // selectable subset from that stable assignment and the latest global
  // healthy-host view.
  const size_t target = std::min<size_t>(subset_size_, all_candidates.size());
  absl::flat_hash_set<Upstream::HostConstSharedPtr> current_hosts(all_candidates.begin(),
                                                                  all_candidates.end());
  absl::flat_hash_set<Upstream::HostConstSharedPtr> retained;
  std::vector<Upstream::HostConstSharedPtr> next;
  next.reserve(target);
  for (const auto& host : random_partition_) {
    if (next.size() == target) {
      break;
    }
    if (current_hosts.contains(host)) {
      next.push_back(host);
      retained.insert(host);
    }
  }

  // Prefer healthy hosts when enough are available, matching initial random
  // partition behavior. If the cluster cannot supply K healthy hosts, sample
  // from all hosts and let the per-worker fallback logic below decide which
  // view to publish.
  const Upstream::HostVector& pool_src =
      (healthy_candidates.size() >= target) ? healthy_candidates : all_candidates;
  std::vector<Upstream::HostConstSharedPtr> pool;
  pool.reserve(pool_src.size());
  for (const auto& host : pool_src) {
    if (!retained.contains(host)) {
      pool.push_back(host);
    }
  }

  for (size_t i = 0; next.size() < target; ++i) {
    const size_t j = i + (random_.random() % (pool.size() - i));
    std::swap(pool[i], pool[j]);
    next.push_back(pool[i]);
  }
  random_partition_ = std::move(next);
}

void PerWorkerSubsetLoadBalancer::rebuildRandomPartition(
    std::vector<Upstream::HostConstSharedPtr>& out) {
  filterAssignmentByHealth(random_partition_, out);
}

void PerWorkerSubsetLoadBalancer::rebuildEqualPartitionAssignment(
    const Upstream::HostVector& candidates) {
  std::vector<Upstream::HostConstSharedPtr> sorted(candidates.begin(), candidates.end());
  std::sort(sorted.begin(), sorted.end(),
            [](const Upstream::HostConstSharedPtr& a, const Upstream::HostConstSharedPtr& b) {
              return a->address()->asString() < b->address()->asString();
            });

  const size_t n = sorted.size();
  if (n == 0) {
    equal_partition_.clear();
    return;
  }

  // K resolution:
  //
  //   ``subset_size > 0 && subset_size >= N`` -> "no subsetting"; each worker
  //   takes the entire cluster (the within-subset LB runs over the full set).
  //   Lets users disable subsetting for small clusters.
  //
  //   Otherwise -> auto-compute ``K = ceil(N / W)``. W comes from
  //   ``total_workers_``, which was resolved at config-load time from
  //   ``bootstrap.concurrency``. Fallback to ``W=1 (K=N)`` if W is somehow
  //   zero -- guarantees the LB still returns something.
  size_t k;
  if (subset_size_ > 0 && subset_size_ >= n) {
    k = n;
  } else {
    k = (total_workers_ > 0) ? (n + total_workers_ - 1) / total_workers_ : n;
  }
  if (k > n) {
    k = n;
  }

  // Worker i starts at ``(envoy_seed + i * k) mod N`` and takes k consecutive
  // hosts wrapping around. With ``K = ceil(N/W)`` and ``worker_id`` in
  // ``[0, W)``, partitions are non-overlapping aside from up to
  // ``W*K - N`` wrap-around double-assignments. The process-local random
  // ``envoy_seed`` rotates the starting position so the "which worker IDs are
  // pinned to host X" mapping varies across the fleet -- a briefly bad host
  // during a backend deploy hits different worker IDs on different Envoys
  // instead of synchronizing on the same ones everywhere. Within a single
  // Envoy, disjointness is preserved.
  //
  // We partition over the sorted ALL-hosts list (stable positions across
  // health changes) and then filter to healthy. This is deliberate: a host
  // flipping unhealthy does not shift any worker's slice boundary; it just
  // drops a single entry from the worker's healthy-subset until the host
  // recovers. The slice itself stays put across rebuilds, so there is no
  // connection-pool churn from membership-position drift.
  const size_t envoy_offset = static_cast<size_t>(envoy_seed_ % n);
  const size_t start = (envoy_offset + static_cast<size_t>(worker_id_) * k) % n;

  equal_partition_.clear();
  equal_partition_.reserve(k);
  for (size_t i = 0; i < k; ++i) {
    equal_partition_.push_back(sorted[(start + i) % n]);
  }
}

void PerWorkerSubsetLoadBalancer::rebuildEqualPartition(
    std::vector<Upstream::HostConstSharedPtr>& out) {
  filterAssignmentByHealth(equal_partition_, out);
}

void PerWorkerSubsetLoadBalancer::filterAssignmentByHealth(
    const std::vector<Upstream::HostConstSharedPtr>& assignment,
    std::vector<Upstream::HostConstSharedPtr>& out) {
  if (assignment.empty()) {
    out.clear();
    return;
  }

  std::vector<Upstream::HostConstSharedPtr> healthy;
  std::vector<Upstream::HostConstSharedPtr> degraded;
  healthy.reserve(assignment.size());
  degraded.reserve(assignment.size());
  for (const auto& host : assignment) {
    switch (host->coarseHealth()) {
    case Upstream::Host::Health::Healthy:
      healthy.push_back(host);
      break;
    case Upstream::Host::Health::Degraded:
      degraded.push_back(host);
      break;
    case Upstream::Host::Health::Unhealthy:
      break;
    }
  }

  const auto meets_threshold = [this, assignment_size = assignment.size()](size_t count) {
    return fallback_threshold_ == 0
               ? count > 0
               : static_cast<uint64_t>(count) * 100ULL >=
                     static_cast<uint64_t>(fallback_threshold_) * assignment_size;
  };
  if (meets_threshold(healthy.size())) {
    out = std::move(healthy);
    return;
  }

  const bool no_healthy_hosts = healthy.empty();
  healthy.insert(healthy.end(), degraded.begin(), degraded.end());
  if (meets_threshold(healthy.size())) {
    out = std::move(healthy);
    return;
  }

  per_worker_subset_stats_.lb_per_worker_subset_slice_fallback_.inc();
  if (no_healthy_hosts) {
    per_worker_subset_stats_.lb_per_worker_subset_slice_empty_healthy_.inc();
  }
  out = assignment;
}

void PerWorkerSubsetLoadBalancer::publishSubsetToSyntheticPrioritySet(
    const std::vector<Upstream::HostConstSharedPtr>& subset) {
  // SimpleRoundRobin reads ``subset_`` directly, no inner LB to feed.
  if (host_selection_strategy_ == HostSelectionStrategy::SimpleRoundRobin) {
    return;
  }

  // The synthetic priority set wants ``HostSharedPtr`` (non-const). The
  // subset stores const refs to the cluster's hosts, which is what the
  // stock LB ultimately reads -- the non-const requirement is only on the
  // priority set's internal vector type, and the LB itself never mutates
  // these. Cast away const for the storage type.
  auto hosts = std::make_shared<Upstream::HostVector>();
  hosts->reserve(subset.size());
  for (const auto& h : subset) {
    hosts->push_back(std::const_pointer_cast<Upstream::Host>(h));
  }

  // Diff against the synthetic set's current hosts to populate
  // ``hosts_added`` / ``hosts_removed``. The stock LB's priority-update
  // callbacks rely on accurate diff lists to keep their per-host state
  // (LeastRequest pending counts, slow-start timers) in sync.
  Upstream::HostVector hosts_added;
  Upstream::HostVector hosts_removed;
  const auto& current_host_sets = synthetic_priority_set_.hostSetsPerPriority();
  if (!current_host_sets.empty()) {
    const auto& current_hosts = current_host_sets[0]->hosts();
    absl::flat_hash_set<Upstream::HostConstSharedPtr> current_set(current_hosts.begin(),
                                                                  current_hosts.end());
    absl::flat_hash_set<Upstream::HostConstSharedPtr> new_set(subset.begin(), subset.end());
    for (const auto& h : *hosts) {
      if (!current_set.contains(h)) {
        hosts_added.push_back(h);
      }
    }
    for (const auto& h : current_hosts) {
      if (!new_set.contains(h)) {
        hosts_removed.push_back(std::const_pointer_cast<Upstream::Host>(h));
      }
    }
  } else {
    hosts_added = *hosts;
  }

  // Ensure priority 0 exists on the synthetic set before ``updateHosts``.
  synthetic_priority_set_.getOrCreateHostSet(0);

  // Single-locality bucket -- locality-weighted LB is not meaningful for a
  // per-worker subset; the cluster's overall locality config is applied at
  // the partition step, not within-subset.
  auto hosts_per_locality = std::make_shared<Upstream::HostsPerLocalityImpl>(*hosts, false);
  auto params = Upstream::HostSetImpl::partitionHosts(hosts, hosts_per_locality);
  synthetic_priority_set_.updateHosts(0, std::move(params), /*locality_weights=*/nullptr,
                                      hosts_added, hosts_removed,
                                      /*weighted_priority_health=*/absl::nullopt,
                                      /*overprovisioning_factor=*/absl::nullopt);
}

Upstream::HostSelectionResponse
PerWorkerSubsetLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  auto current = subset_;
  if (!current || current->empty()) {
    // No selectable hosts -- ``lb_per_worker_subset_slice_fallback`` was
    // already incremented during ``rebuildSubset()`` when applicable; just
    // count the user-visible null return here.
    per_worker_subset_stats_.lb_per_worker_subset_empty_returns_.inc();
    return {nullptr};
  }

  if (inner_lb_ != nullptr) {
    return inner_lb_->chooseHost(context);
  }
  return chooseHostSimpleRoundRobin(*current, context);
}

Upstream::HostSelectionResponse PerWorkerSubsetLoadBalancer::chooseHostSimpleRoundRobin(
    const std::vector<Upstream::HostConstSharedPtr>& subset,
    Upstream::LoadBalancerContext* context) {
  const size_t n = subset.size();
  for (size_t attempts = 0; attempts < n; ++attempts) {
    const uint64_t idx = next_index_.fetch_add(1, std::memory_order_relaxed) % n;
    const Upstream::HostConstSharedPtr& candidate = subset[idx];
    if (context == nullptr || !context->shouldSelectAnotherHost(*candidate)) {
      return {candidate};
    }
  }
  return {nullptr};
}

} // namespace PerWorkerSubset
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
