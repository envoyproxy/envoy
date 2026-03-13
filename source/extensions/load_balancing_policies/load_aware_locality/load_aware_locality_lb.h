#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/random_generator.h"
#include "envoy/event/dispatcher.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/thread_local/thread_local_object.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/upstream_impl.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {

class PriorityLoadEvaluator;

/**
 * Load balancer config for the load-aware locality policy.
 */
// Shared ownership wrapper for the child LB config. The config is created during loadConfig()
// and shared between LoadAwareLocalityLbConfig (which is const when accessed) and
// WorkerLocalLbFactory (which needs it for the lifetime of the cluster).
using LoadBalancerConfigSharedPtr = std::shared_ptr<Upstream::LoadBalancerConfig>;

class LoadAwareLocalityLbConfig : public Upstream::LoadBalancerConfig {
public:
  LoadAwareLocalityLbConfig(Upstream::TypedLoadBalancerFactory& endpoint_picking_policy_factory,
                            std::string endpoint_picking_policy_name,
                            LoadBalancerConfigSharedPtr endpoint_picking_policy_config,
                            std::chrono::milliseconds weight_update_period,
                            double utilization_variance_threshold, double ewma_alpha,
                            double probe_percentage,
                            std::chrono::milliseconds weight_expiration_period,
                            Event::Dispatcher& main_thread_dispatcher,
                            ThreadLocal::SlotAllocator& tls_slot_allocator)
      : endpoint_picking_policy_factory_(endpoint_picking_policy_factory),
        endpoint_picking_policy_name_(std::move(endpoint_picking_policy_name)),
        endpoint_picking_policy_config_(std::move(endpoint_picking_policy_config)),
        weight_update_period_(weight_update_period),
        utilization_variance_threshold_(utilization_variance_threshold), ewma_alpha_(ewma_alpha),
        probe_percentage_(probe_percentage), weight_expiration_period_(weight_expiration_period),
        main_thread_dispatcher_(main_thread_dispatcher), tls_slot_allocator_(tls_slot_allocator) {}

  Upstream::TypedLoadBalancerFactory& endpointPickingPolicyFactory() const {
    return endpoint_picking_policy_factory_;
  }
  const std::string& endpointPickingPolicyName() const { return endpoint_picking_policy_name_; }
  const LoadBalancerConfigSharedPtr& endpointPickingPolicyConfig() const {
    return endpoint_picking_policy_config_;
  }
  std::chrono::milliseconds weightUpdatePeriod() const { return weight_update_period_; }
  double utilizationVarianceThreshold() const { return utilization_variance_threshold_; }
  double ewmaAlpha() const { return ewma_alpha_; }
  double probePercentage() const { return probe_percentage_; }
  std::chrono::milliseconds weightExpirationPeriod() const { return weight_expiration_period_; }
  Event::Dispatcher& mainThreadDispatcher() const { return main_thread_dispatcher_; }
  ThreadLocal::SlotAllocator& tlsSlotAllocator() const { return tls_slot_allocator_; }

private:
  Upstream::TypedLoadBalancerFactory& endpoint_picking_policy_factory_;
  const std::string endpoint_picking_policy_name_;
  LoadBalancerConfigSharedPtr endpoint_picking_policy_config_;
  std::chrono::milliseconds weight_update_period_;
  double utilization_variance_threshold_;
  double ewma_alpha_;
  double probe_percentage_;
  std::chrono::milliseconds weight_expiration_period_;
  Event::Dispatcher& main_thread_dispatcher_;
  ThreadLocal::SlotAllocator& tls_slot_allocator_;
};

/**
 * Immutable snapshot of per-locality routing weights for a single priority.
 */
struct PriorityRoutingWeights {
  // Per-locality routing weights (host_count * headroom), indexed by HostsPerLocality order.
  std::vector<double> weights;
  // Sum of all weights for weighted random selection.
  double total_weight{0.0};
  // Informational flag: true if local preference was triggered (variance below threshold).
  // Not consulted on the hot path — the routing decision is fully encoded in weights[].
  // With probe_percentage > 0, weights[] still include a small remote share even when true.
  bool all_local{false};
  // True when the hosts-per-locality has a local locality (index 0 is "local").
  // Workers use this to pick the correct zone routing stat counter.
  bool has_local_locality{false};
  // Degraded-only routing weights. Used when Envoy chooses degraded hosts within this priority.
  std::vector<double> degraded_weights;
  double degraded_total_weight{0.0};
  bool degraded_all_local{false};
  // All-host routing weights. Used when the priority is in panic and Envoy can select all hosts.
  std::vector<double> all_host_weights;
  double all_host_total_weight{0.0};
  bool all_host_all_local{false};

  enum class SelectionSource : uint8_t { Healthy = 0, Degraded = 1, AllHosts = 2 };

  const std::vector<double>& weightsFor(SelectionSource source) const {
    switch (source) {
    case SelectionSource::Healthy:
      return weights;
    case SelectionSource::Degraded:
      return degraded_weights;
    case SelectionSource::AllHosts:
      return all_host_weights;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  double totalWeightFor(SelectionSource source) const {
    switch (source) {
    case SelectionSource::Healthy:
      return total_weight;
    case SelectionSource::Degraded:
      return degraded_total_weight;
    case SelectionSource::AllHosts:
      return all_host_total_weight;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  bool allLocalFor(SelectionSource source) const {
    switch (source) {
    case SelectionSource::Healthy:
      return all_local;
    case SelectionSource::Degraded:
      return degraded_all_local;
    case SelectionSource::AllHosts:
      return all_host_all_local;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
};

/**
 * Immutable snapshot of routing state shared between the main thread and workers.
 */
struct RoutingWeightsSnapshot {
  // Legacy view of priority 0 for tests and single-priority fast paths.
  std::vector<double> weights;
  double total_weight{0.0};
  bool all_local{false};
  bool has_local_locality{false};
  // Per-priority routing weights, indexed by cluster priority.
  std::vector<PriorityRoutingWeights> priority_weights;
  // Cluster-level priority distribution used to preserve Envoy failover semantics.
  Upstream::HealthyAndDegradedLoad priority_loads;
  // Per-priority panic state from Envoy's priority evaluator.
  std::vector<bool> priority_panic;
};

using RoutingWeightsSnapshotConstSharedPtr = std::shared_ptr<const RoutingWeightsSnapshot>;

/**
 * Thread-local shim holding the routing weights pointer, pushed from the main thread via TLS.
 */
struct ThreadLocalShim : public ThreadLocal::ThreadLocalObject {
  RoutingWeightsSnapshotConstSharedPtr routing_weights;
};

class WorkerLocalLb;

/**
 * Factory shared across workers. Holds routing weights and child policy factory.
 * The main thread updates routing weights; workers read them to create WorkerLocalLb instances.
 */
class WorkerLocalLbFactory : public Upstream::LoadBalancerFactory {
public:
  WorkerLocalLbFactory(Upstream::TypedLoadBalancerFactory& child_factory,
                       std::string child_factory_name, LoadBalancerConfigSharedPtr child_config,
                       const Upstream::ClusterInfo& cluster_info,
                       const Upstream::PrioritySet& cluster_priority_set, Runtime::Loader& runtime,
                       Envoy::Random::RandomGenerator& random, TimeSource& time_source,
                       ThreadLocal::SlotAllocator& tls_slot_allocator);

  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override;
  bool recreateOnHostChange() const override { return false; }

  // Called by the main thread (from LoadAwareLocalityLoadBalancer::initialize()) to initialize
  // the shared child ThreadAwareLoadBalancer. Must be called before any worker creates a LB.
  absl::Status initializeChildLb();

  // Called by the main thread to publish new routing weights via TLS.
  void updateRoutingWeights(RoutingWeightsSnapshotConstSharedPtr snapshot) {
    tls_->runOnAllThreads([snapshot](OptRef<ThreadLocalShim> shim) {
      if (shim.has_value()) {
        shim->routing_weights = snapshot;
      }
    });
  }

  // Called by workers to get the current routing weights (lock-free TLS read).
  // SAFETY: Must only be called on a thread that owns a TLS slot instance (worker or main
  // thread). The returned pointer is valid only for the duration of the current task; do not
  // store it across yield points or after the TLS slot could be updated.
  const RoutingWeightsSnapshot* routingWeights() const {
    auto shim = tls_->get();
    return shim.has_value() ? shim->routing_weights.get() : nullptr;
  }

  // Called by workers to create a per-locality worker LB from the shared child factory.
  // Must only be called after initializeChildLb() has returned on the main thread.
  Upstream::LoadBalancerPtr
  createWorkerChildLb(Upstream::PrioritySetImpl& per_locality_priority_set);

  // Whether the child policy requires the worker LB to be recreated on host membership changes.
  bool recreateChildOnHostChange() const;

  Envoy::Random::RandomGenerator& random() const { return random_; }
  Upstream::ClusterLbStats& lbStats() const { return cluster_info_.lbStats(); }

private:
  std::string child_factory_name_;
  LoadBalancerConfigSharedPtr child_config_;
  const Upstream::ClusterInfo& cluster_info_;
  Envoy::Random::RandomGenerator& random_;

  // Single child ThreadAwareLoadBalancer created and initialized on the main thread.
  // Workers only call factory()->create() from it.
  Upstream::ThreadAwareLoadBalancerPtr child_thread_aware_lb_;

  std::unique_ptr<ThreadLocal::TypedSlot<ThreadLocalShim>> tls_;
};

/**
 * Per-locality state in the worker-local LB. Holds a child PrioritySet and child LB for
 * one locality.
 */
struct PerSourceLocalityState {
  // PrioritySet containing only the hosts that can be selected for one source in one locality.
  std::unique_ptr<Upstream::PrioritySetImpl> priority_set;
  // The worker-local LB for this source/locality pair, created from the shared child factory.
  Upstream::LoadBalancerPtr lb;
};

struct PerLocalityState {
  PerSourceLocalityState healthy;
  PerSourceLocalityState degraded;
  PerSourceLocalityState all_hosts;

  PerSourceLocalityState& stateFor(PriorityRoutingWeights::SelectionSource source) {
    switch (source) {
    case PriorityRoutingWeights::SelectionSource::Healthy:
      return healthy;
    case PriorityRoutingWeights::SelectionSource::Degraded:
      return degraded;
    case PriorityRoutingWeights::SelectionSource::AllHosts:
      return all_hosts;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }

  const PerSourceLocalityState& stateFor(PriorityRoutingWeights::SelectionSource source) const {
    switch (source) {
    case PriorityRoutingWeights::SelectionSource::Healthy:
      return healthy;
    case PriorityRoutingWeights::SelectionSource::Degraded:
      return degraded;
    case PriorityRoutingWeights::SelectionSource::AllHosts:
      return all_hosts;
    }
    PANIC_DUE_TO_CORRUPT_ENUM;
  }
};

struct PerPriorityLocalityState {
  std::vector<PerLocalityState> localities;
};

struct SelectedPriority {
  uint32_t priority;
  PriorityRoutingWeights::SelectionSource source;
};

/**
 * Worker-local load balancer. Selects a locality by capacity-weighted random, then delegates
 * to the per-locality child LB for endpoint selection.
 */
class WorkerLocalLb : public Upstream::LoadBalancer,
                      protected Logger::Loggable<Logger::Id::upstream> {
public:
  WorkerLocalLb(WorkerLocalLbFactory& factory, const Upstream::PrioritySet& priority_set);
  ~WorkerLocalLb() override;

  // Upstream::LoadBalancer
  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext* context) override;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override;
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext* context, const Upstream::Host& host,
                           std::vector<uint8_t>& hash_key) override;

private:
  // Build per-locality child LBs for all priorities.
  void buildPerPriorityLocalities();

  // Build or rebuild the per-locality child LBs for a single priority.
  void buildPerLocality(uint32_t priority, const Upstream::HostSet& host_set);

  // Handle incremental host membership changes for the given priority.
  void onHostChange(uint32_t priority);

  // Handle priority updates without host deltas by re-partitioning each locality so child LBs
  // observe in-place host changes (for example health, weight, or metadata updates).
  void onInPlaceHostUpdate(uint32_t priority);

  // Update a locality/source PrioritySet with a pre-selected host subset.
  void updateLocalityHosts(PerSourceLocalityState& state, const Upstream::HostVector& hosts,
                           bool is_local, const Upstream::HostVector& hosts_added,
                           const Upstream::HostVector& hosts_removed);

  // Update the per-source child LBs for one locality from the cluster's current host set.
  void syncLocalityState(PerLocalityState& state, const Upstream::HostSet& host_set,
                         size_t locality_index, bool recreate_child);

  // Choose the cluster priority to route to.
  absl::optional<SelectedPriority> choosePriority() const;

  // Choose a locality index within the selected priority/source.
  size_t chooseLocality(uint32_t priority, PriorityRoutingWeights::SelectionSource source) const;

  // Select a locality index using weighted random based on routing weights.
  size_t selectLocality(const PriorityRoutingWeights& snapshot,
                        PriorityRoutingWeights::SelectionSource source,
                        const std::vector<PerLocalityState>& per_locality) const;

  // Find a usable child LB at the preferred locality, falling back to any locality with a
  // non-null LB when the routing snapshot is stale (e.g. a locality's hosts were removed but
  // the routing weights haven't been recomputed yet). Returns nullptr if no locality has a
  // usable LB. Sets actual_idx to the index of the locality whose LB was returned.
  Upstream::LoadBalancer* pickLocalityLb(const std::vector<PerLocalityState>& per_locality,
                                         PriorityRoutingWeights::SelectionSource source,
                                         size_t preferred_idx, size_t& actual_idx) const;

  // Best-effort fallback for transient snapshot/worker mismatches.
  absl::optional<SelectedPriority> firstAvailablePriority() const;

  WorkerLocalLbFactory& factory_;
  const Upstream::PrioritySet& priority_set_;
  Upstream::ClusterLbStats& stats_;
  std::vector<PerPriorityLocalityState> per_priority_locality_;
  // Destroyed explicitly in the destructor before other members so the callback doesn't fire
  // during destruction and access freed per-locality state.
  Envoy::Common::CallbackHandlePtr member_update_cb_;
};

/**
 * Main thread load balancer. Reads host-level orcaUtilization() and computes locality routing
 * weights on a periodic timer.
 */
class LoadAwareLocalityLoadBalancer : public Upstream::ThreadAwareLoadBalancer,
                                      protected Logger::Loggable<Logger::Id::upstream> {
public:
  LoadAwareLocalityLoadBalancer(OptRef<const Upstream::LoadBalancerConfig> lb_config,
                                const Upstream::ClusterInfo& cluster_info,
                                const Upstream::PrioritySet& priority_set, Runtime::Loader& runtime,
                                Envoy::Random::RandomGenerator& random, TimeSource& time_source);
  ~LoadAwareLocalityLoadBalancer() override;

  // Upstream::ThreadAwareLoadBalancer
  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override;

private:
  // Compute per-locality routing weights from host-level orcaUtilization() and publish to factory.
  void computeLocalityRoutingWeights();

  const Upstream::PrioritySet& priority_set_;
  Upstream::ClusterLbStats& stats_;
  TimeSource& time_source_;
  double utilization_variance_threshold_;
  double ewma_alpha_;
  double probe_percentage_;
  std::chrono::milliseconds weight_expiration_period_;
  // Per-source, per-priority, per-locality EWMA-smoothed utilization state (main thread only).
  std::array<std::vector<std::vector<double>>, 3> smoothed_utilizations_;
  std::array<std::vector<std::vector<bool>>, 3> smoothed_utilizations_valid_;
  // Scratch buffers reused across timer callback
  std::vector<double> avg_utils_;
  std::vector<uint32_t> valid_counts_;
  std::vector<uint32_t> host_counts_;
  Event::TimerPtr weight_update_timer_;
  std::chrono::milliseconds weight_update_period_;
  std::unique_ptr<PriorityLoadEvaluator> priority_load_evaluator_;
  std::shared_ptr<WorkerLocalLbFactory> factory_;
};

} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
