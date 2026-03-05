#pragma once

#include <memory>
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
                            LoadBalancerConfigSharedPtr endpoint_picking_policy_config,
                            std::chrono::milliseconds weight_update_period,
                            double utilization_variance_threshold, double ewma_alpha,
                            double probe_percentage, Event::Dispatcher& main_thread_dispatcher,
                            ThreadLocal::SlotAllocator& tls_slot_allocator)
      : endpoint_picking_policy_factory_(endpoint_picking_policy_factory),
        endpoint_picking_policy_config_(std::move(endpoint_picking_policy_config)),
        weight_update_period_(weight_update_period),
        utilization_variance_threshold_(utilization_variance_threshold), ewma_alpha_(ewma_alpha),
        probe_percentage_(probe_percentage), main_thread_dispatcher_(main_thread_dispatcher),
        tls_slot_allocator_(tls_slot_allocator) {}

  Upstream::TypedLoadBalancerFactory& endpointPickingPolicyFactory() const {
    return endpoint_picking_policy_factory_;
  }
  const LoadBalancerConfigSharedPtr& endpointPickingPolicyConfig() const {
    return endpoint_picking_policy_config_;
  }
  std::chrono::milliseconds weightUpdatePeriod() const { return weight_update_period_; }
  double utilizationVarianceThreshold() const { return utilization_variance_threshold_; }
  double ewmaAlpha() const { return ewma_alpha_; }
  double probePercentage() const { return probe_percentage_; }
  Event::Dispatcher& mainThreadDispatcher() const { return main_thread_dispatcher_; }
  ThreadLocal::SlotAllocator& tlsSlotAllocator() const { return tls_slot_allocator_; }

private:
  Upstream::TypedLoadBalancerFactory& endpoint_picking_policy_factory_;
  LoadBalancerConfigSharedPtr endpoint_picking_policy_config_;
  std::chrono::milliseconds weight_update_period_;
  double utilization_variance_threshold_;
  double ewma_alpha_;
  double probe_percentage_;
  Event::Dispatcher& main_thread_dispatcher_;
  ThreadLocal::SlotAllocator& tls_slot_allocator_;
};

/**
 * Immutable snapshot of per-locality routing weights, shared between main thread and workers.
 */
struct RoutingWeightsSnapshot {
  // Per-locality routing weights (host_count * headroom), indexed by HostsPerLocality order.
  std::vector<double> weights;
  // Sum of all weights for weighted random selection.
  double total_weight{0.0};
  // Informational flag: true if local preference was triggered (variance below threshold).
  // Not consulted on the hot path — the routing decision is fully encoded in weights[].
  // With probe_percentage > 0, weights[] still include a small remote share even when true.
  bool all_local{false};
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
                       LoadBalancerConfigSharedPtr child_config,
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
  // Returns a raw pointer — lifetime is guaranteed by the TLS slot, no atomic ops needed.
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

private:
  Upstream::TypedLoadBalancerFactory& child_factory_;
  LoadBalancerConfigSharedPtr child_config_;
  const Upstream::ClusterInfo& cluster_info_;
  Runtime::Loader& runtime_;
  Envoy::Random::RandomGenerator& random_;
  TimeSource& time_source_;

  // Single child ThreadAwareLoadBalancer created and initialized on the main thread.
  // Workers only call factory()->create() from it.
  Upstream::ThreadAwareLoadBalancerPtr child_thread_aware_lb_;

  std::unique_ptr<ThreadLocal::TypedSlot<ThreadLocalShim>> tls_;
};

/**
 * Per-locality state in the worker-local LB. Holds a child PrioritySet and child LB for
 * one locality.
 */
struct PerLocalityState {
  // PrioritySet containing only this locality's hosts.
  std::unique_ptr<Upstream::PrioritySetImpl> priority_set;
  // The worker-local LB for this locality, created from the shared child factory.
  Upstream::LoadBalancerPtr lb;
};

/**
 * Worker-local load balancer. Selects a locality by capacity-weighted random, then delegates
 * to the per-locality child LB for endpoint selection.
 */
class WorkerLocalLb : public Upstream::LoadBalancer,
                      protected Logger::Loggable<Logger::Id::upstream> {
public:
  WorkerLocalLb(WorkerLocalLbFactory& factory, const Upstream::PrioritySet& priority_set);

  // Upstream::LoadBalancer
  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext* context) override;
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override;
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext* context, const Upstream::Host& host,
                           std::vector<uint8_t>& hash_key) override;

private:
  // Build per-locality child LBs from the given hosts-per-locality.
  void buildPerLocality(const Upstream::HostsPerLocality& hosts_per_locality);

  // Handle incremental host membership changes for the given priority.
  void onHostChange(uint32_t priority);

  // Handle health-only updates (no membership change) by re-partitioning each locality.
  void onHealthChange(uint32_t priority);

  // Update a locality's PrioritySet with new hosts and partition parameters.
  void updateLocalityHosts(PerLocalityState& state, const Upstream::HostVector& hosts,
                           bool is_local, const Upstream::HostVector& hosts_added,
                           const Upstream::HostVector& hosts_removed);

  // Choose a locality index considering routing weights and single-locality case.
  // Returns 0 if there are no routing weights or only one locality.
  size_t chooseLocality();

  // Select a locality index using weighted random based on routing weights.
  size_t selectLocality(const RoutingWeightsSnapshot& snapshot);

  WorkerLocalLbFactory& factory_;
  const Upstream::PrioritySet& priority_set_;
  std::vector<PerLocalityState> per_locality_;
  // Must be declared LAST — destroyed first so the callback doesn't fire during destruction
  // and access freed per-locality state.
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

  // Upstream::ThreadAwareLoadBalancer
  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override;

private:
  // Compute per-locality routing weights from host-level orcaUtilization() and publish to factory.
  void computeLocalityRoutingWeights();

  const Upstream::PrioritySet& priority_set_;
  double utilization_variance_threshold_;
  double ewma_alpha_;
  double probe_percentage_;
  // Per-locality EWMA-smoothed utilization state (main thread only).
  std::vector<double> smoothed_utilizations_;
  // Scratch buffers reused across timer callback
  std::vector<double> avg_utils_;
  std::vector<uint32_t> valid_counts_;
  std::vector<uint32_t> host_counts_;
  Event::TimerPtr weight_update_timer_;
  std::chrono::milliseconds weight_update_period_;
  std::shared_ptr<WorkerLocalLbFactory> factory_;
};

} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
