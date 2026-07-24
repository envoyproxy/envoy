#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/extensions/load_balancing_policies/common/v3/common.pb.h"
#include "envoy/http/conn_pool.h"
#include "envoy/runtime/runtime.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/upstream_impl.h"

#include "contrib/envoy/extensions/load_balancing_policies/per_worker_subset/v3alpha/per_worker_subset.pb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PerWorkerSubset {

using PerWorkerSubsetLbProto =
    envoy::extensions::load_balancing_policies::per_worker_subset::v3alpha::PerWorkerSubset;

// Per-cluster stats emitted under
// ``cluster.<cluster_name>.lb_per_worker_subset_*``. Naming follows the
// ``cluster.<name>.lb_<lbname>_<event>`` convention used by other upstream
// LB extensions (see ``lb_subsets_*`` on the subset LB).
//
//   lb_per_worker_subset_rebuilds
//       Increments on every ``rebuildSubset()`` (per worker, summed).
//   lb_per_worker_subset_slice_fallback
//       Increments when a worker's slice has too few healthy hosts and the
//       LB falls back to including unhealthy hosts in the subset. Per-worker;
//       workers fall back independently based on their own slice's health.
//   lb_per_worker_subset_slice_empty_healthy
//       Increments when a worker's slice has zero healthy hosts (a subset
//       of ``slice_fallback`` events; tells operators the fallback was
//       forced, not just threshold-tripped).
//   lb_per_worker_subset_empty_returns
//       Increments when ``chooseHost()`` returned nullptr because the
//       subset was empty.
#define ALL_PER_WORKER_SUBSET_STATS(COUNTER)                                                       \
  COUNTER(lb_per_worker_subset_rebuilds)                                                           \
  COUNTER(lb_per_worker_subset_slice_fallback)                                                     \
  COUNTER(lb_per_worker_subset_slice_empty_healthy)                                                \
  COUNTER(lb_per_worker_subset_empty_returns)

struct PerWorkerSubsetStats {
  ALL_PER_WORKER_SUBSET_STATS(GENERATE_COUNTER_STRUCT)
};

enum class PartitioningStrategy : uint8_t {
  EqualPartitions = 0,
  RandomPartitions = 1,
};

// Within-subset host selection -- applied on top of either partitioning
// strategy. Unspecified is the proto's default (the field is unset / left at
// its zero value) and is rejected at config-load time when the extension is
// actually being instantiated.
enum class HostSelectionStrategy : uint8_t {
  Unspecified = 0,
  // Minimal ``next_index_ % K`` round-robin implemented inline. No weight
  // handling, no slow_start, no locality.
  SimpleRoundRobin = 1,
  // Envoy's stock RoundRobin LB constructed against the worker's subset via
  // a synthetic PrioritySet. Honors per-host weights (EDF), slow_start_config,
  // and locality-weighted LB.
  EnvoyRoundRobin = 2,
  // Power-of-two-choices on active request counts. Wraps Envoy's stock
  // LeastRequestLoadBalancer (Envoy's class name is historical; the algorithm
  // is P2C with choice_count=2).
  EnvoyP2C = 3,
};

class TypedPerWorkerSubsetLbConfig : public Upstream::LoadBalancerConfig {
public:
  TypedPerWorkerSubsetLbConfig(const PerWorkerSubsetLbProto& proto, uint32_t total_workers,
                               uint64_t envoy_seed)
      : proto_(proto), total_workers_(total_workers), envoy_seed_(envoy_seed) {}

  const PerWorkerSubsetLbProto proto_;

  // Resolved total worker count W, set once at config-load time from
  // ``bootstrap.concurrency`` (with ``hardware_concurrency()`` fallback).
  // EQUAL_PARTITIONS uses this for ``K = ceil(N/W)``. Constant across the
  // lifetime of the cluster.
  const uint32_t total_workers_;

  // Process-local random seed used by EQUAL_PARTITIONS to rotate the starting
  // host index. The LB applies
  // ``start = (envoy_seed % N + worker_id * K) % N`` so different Envoy
  // processes rotate the partition assignment. This spreads "which worker
  // IDs are pinned to which host" across the fleet so a brief bad host during
  // a backend deploy does not land on the exact same worker IDs on every box.
  // Within a single Envoy, disjointness is preserved.
  const uint64_t envoy_seed_;

  // Sequential per-cluster worker-ID counter. Each per-worker LB instance
  // grabs its ID via ``fetch_add`` (returns pre-increment value). Solely for
  // unique IDs; W comes from ``total_workers_`` above.
  mutable std::atomic<uint32_t> next_worker_id_{0};
};

class PerWorkerSubsetLoadBalancer : public Upstream::LoadBalancer,
                                    public Logger::Loggable<Logger::Id::upstream> {
public:
  PerWorkerSubsetLoadBalancer(
      const Upstream::PrioritySet& priority_set, Upstream::ClusterLbStats& stats,
      Stats::Scope& scope, Runtime::Loader& runtime, ::Envoy::Random::RandomGenerator& random,
      TimeSource& time_source, uint32_t subset_size, PartitioningStrategy strategy,
      HostSelectionStrategy host_selection_strategy, uint32_t worker_id, uint32_t total_workers,
      uint32_t fallback_threshold, uint64_t envoy_seed,
      const envoy::extensions::load_balancing_policies::common::v3::SlowStartConfig&
          slow_start_config);

  // Upstream::LoadBalancer
  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
    return nullptr;
  }
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext*, const Upstream::Host&,
                           std::vector<uint8_t>&) override {
    return absl::nullopt;
  }
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
    return {};
  }

private:
  void rebuildSubset(bool membership_changed);
  // Reconcile RANDOM_PARTITIONS' stable assignment after a membership update:
  // retain sampled hosts that still exist and fill only vacancies. On initial
  // construction this samples the complete assignment.
  void reconcileRandomPartition(const Upstream::HostVector& healthy_candidates,
                                const Upstream::HostVector& all_candidates);
  // Apply the shared health/degraded/fallback policy to the stable random
  // assignment without resampling it.
  void rebuildRandomPartition(std::vector<Upstream::HostConstSharedPtr>& out);
  // Recompute EQUAL_PARTITIONS' stable K-host assignment after a membership
  // update. This is the only path that copies and address-sorts all N hosts.
  void rebuildEqualPartitionAssignment(const Upstream::HostVector& all_candidates);
  // Filter the cached equal assignment to healthy hosts and apply the
  // per-worker fallback threshold. Health-only updates call only this O(K)
  // path.
  void rebuildEqualPartition(std::vector<Upstream::HostConstSharedPtr>& out);
  // Apply the common healthy -> degraded -> full-assignment selection policy.
  // Healthy hosts are preferred; degraded hosts are added only when healthy
  // capacity does not meet the per-worker threshold. Unhealthy hosts are
  // included only in the final panic-style fallback.
  void filterAssignmentByHealth(const std::vector<Upstream::HostConstSharedPtr>& assignment,
                                std::vector<Upstream::HostConstSharedPtr>& out);

  // Simple in-extension pick: ``next_index_ % K`` round-robin against the
  // worker's subset. Used when ``host_selection_strategy_`` is
  // ``SimpleRoundRobin``.
  Upstream::HostSelectionResponse
  chooseHostSimpleRoundRobin(const std::vector<Upstream::HostConstSharedPtr>& subset,
                             Upstream::LoadBalancerContext* context);

  // Push the latest subset into ``synthetic_priority_set_`` so the inner
  // stock LB (if any) sees an updated host set and re-derives. No-op when
  // ``host_selection_strategy_`` is ``SimpleRoundRobin``.
  void publishSubsetToSyntheticPrioritySet(const std::vector<Upstream::HostConstSharedPtr>& subset);

  const Upstream::PrioritySet& priority_set_;
  Upstream::ClusterLbStats& stats_;
  // Extension-specific counters under ``cluster.X.per_worker_subset.*``.
  // Members are bound to symbol-table refs at construction; subsequent
  // ``inc()`` calls are lock-free.
  PerWorkerSubsetStats per_worker_subset_stats_;
  Runtime::Loader& runtime_;
  ::Envoy::Random::RandomGenerator& random_;
  TimeSource& time_source_;
  const uint32_t subset_size_;
  const PartitioningStrategy strategy_;
  const HostSelectionStrategy host_selection_strategy_;
  const uint32_t worker_id_;

  // Total worker count W (fixed at config-load time from
  // ``bootstrap.concurrency`` or hardware CPU count). Used by
  // EQUAL_PARTITIONS to compute ``K = ceil(N/W)``.
  const uint32_t total_workers_;

  // Per-worker fallback threshold (percent points). Healthy hosts are
  // preferred; degraded hosts are added when healthy capacity is below the
  // threshold. Unhealthy hosts are included only when healthy plus degraded
  // capacity still falls short. Range ``[0, 100]``; 0 disables the percent
  // check while preserving healthy -> degraded -> full-slice preference.
  const uint32_t fallback_threshold_;

  // Per-Envoy seed for EQUAL_PARTITIONS -- see TypedPerWorkerSubsetLbConfig.
  const uint64_t envoy_seed_;

  // Stable K-host assignment for EQUAL_PARTITIONS. Membership updates
  // recompute it; health-only updates filter it without copying or sorting
  // the cluster-wide N-host list.
  std::vector<Upstream::HostConstSharedPtr> equal_partition_;

  // Stable host assignment for RANDOM_PARTITIONS. Health-only updates filter
  // this assignment but never resample it; membership updates retain surviving
  // hosts and fill only vacancies.
  std::vector<Upstream::HostConstSharedPtr> random_partition_;

  std::shared_ptr<const std::vector<Upstream::HostConstSharedPtr>> subset_;
  std::atomic<uint64_t> next_index_{0};
  ::Envoy::Common::CallbackHandlePtr member_update_cb_;

  // Synthetic priority set populated with the worker's current subset on
  // every rebuild. Empty / unused when ``host_selection_strategy_`` is
  // ``SimpleRoundRobin`` (no inner LB to feed). Constructed before
  // ``inner_lb_`` so the inner LB's constructor can install its
  // priority-update callbacks against it.
  Upstream::PrioritySetImpl synthetic_priority_set_;
  // Inner stock LB instance -- null for SimpleRoundRobin. EnvoyRoundRobin
  // and EnvoyP2C both delegate ``chooseHost`` here.
  Upstream::LoadBalancerPtr inner_lb_;
};

} // namespace PerWorkerSubset
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
