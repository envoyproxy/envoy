#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/random_generator.h"
#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/thread_local/thread_local_object.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/locality.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {

// Per-host ORCA data written by workers and read by the main-thread weight computation.
// The release/acquire timestamp store publishes the preceding utilization store.
class LocalityLbHostData : public Upstream::HostLbPolicyData {
public:
  // Out-of-band sentinel so monotonic time 0 remains a valid report timestamp.
  static constexpr MonotonicTime kNeverReported = MonotonicTime::min();

  LocalityLbHostData(TimeSource& time_source,
                     std::shared_ptr<const std::vector<std::string>> metric_names)
      : time_source_(time_source), metric_names_(std::move(metric_names)) {}

  bool receivesOrcaLoadReport() const override { return true; }

  absl::Status onOrcaLoadReport(const Upstream::OrcaLoadReport& report,
                                const StreamInfo::StreamInfo&) override;

  double utilization() const { return utilization_.load(std::memory_order_relaxed); }
  MonotonicTime lastUpdateTime() const { return last_update_time_.load(std::memory_order_acquire); }

private:
  void storeUtilization(double util, MonotonicTime now) {
    if (!std::isfinite(util)) {
      return;
    }
    utilization_.store(std::clamp(util, 0.0, 1.0), std::memory_order_relaxed);
    last_update_time_.store(now, std::memory_order_release);
  }

  static_assert(std::atomic<double>::is_always_lock_free,
                "std::atomic<double> must be lock-free for safe cross-thread utilization updates");
  static_assert(std::atomic<MonotonicTime>::is_always_lock_free,
                "std::atomic<MonotonicTime> must be lock-free for safe cross-thread freshness "
                "updates");
  std::atomic<double> utilization_{0.0};
  std::atomic<MonotonicTime> last_update_time_{kNeverReported};
  TimeSource& time_source_;
  const std::shared_ptr<const std::vector<std::string>> metric_names_;
};

// Shared between config and worker factory; must outlive the child LB.
using LoadBalancerConfigSharedPtr = std::shared_ptr<Upstream::LoadBalancerConfig>;

/**
 * Load balancer config for the load-aware locality policy.
 */
class LoadAwareLocalityLbConfig : public Upstream::LoadBalancerConfig {
public:
  LoadAwareLocalityLbConfig(Upstream::TypedLoadBalancerFactory& endpoint_picking_policy_factory,
                            LoadBalancerConfigSharedPtr endpoint_picking_policy_config,
                            std::chrono::milliseconds weight_update_period,
                            double utilization_variance_threshold, double ewma_alpha,
                            double remote_probe_fraction,
                            std::chrono::milliseconds weight_expiration_period,
                            std::vector<std::string> metric_names_for_computing_utilization,
                            Event::Dispatcher& main_thread_dispatcher,
                            ThreadLocal::SlotAllocator& tls_slot_allocator)
      : endpoint_picking_policy_factory_(endpoint_picking_policy_factory),
        endpoint_picking_policy_config_(std::move(endpoint_picking_policy_config)),
        weight_update_period_(weight_update_period),
        utilization_variance_threshold_(utilization_variance_threshold), ewma_alpha_(ewma_alpha),
        remote_probe_fraction_(remote_probe_fraction),
        weight_expiration_period_(weight_expiration_period),
        metric_names_for_computing_utilization_(std::move(metric_names_for_computing_utilization)),
        main_thread_dispatcher_(main_thread_dispatcher), tls_slot_allocator_(tls_slot_allocator) {}

  Upstream::TypedLoadBalancerFactory& endpointPickingPolicyFactory() const {
    return endpoint_picking_policy_factory_;
  }
  std::string endpointPickingPolicyName() const { return endpoint_picking_policy_factory_.name(); }
  const LoadBalancerConfigSharedPtr& endpointPickingPolicyConfig() const {
    return endpoint_picking_policy_config_;
  }
  std::chrono::milliseconds weightUpdatePeriod() const { return weight_update_period_; }
  double utilizationVarianceThreshold() const { return utilization_variance_threshold_; }
  double ewmaAlpha() const { return ewma_alpha_; }
  double remoteProbeFraction() const { return remote_probe_fraction_; }
  std::chrono::milliseconds weightExpirationPeriod() const { return weight_expiration_period_; }
  const std::vector<std::string>& metricNamesForComputingUtilization() const {
    return metric_names_for_computing_utilization_;
  }
  Event::Dispatcher& mainThreadDispatcher() const { return main_thread_dispatcher_; }
  ThreadLocal::SlotAllocator& tlsSlotAllocator() const { return tls_slot_allocator_; }
  absl::Status validateEndpoints(const Upstream::PriorityState& priorities) const override {
    return endpoint_picking_policy_config_ != nullptr
               ? endpoint_picking_policy_config_->validateEndpoints(priorities)
               : absl::OkStatus();
  }

private:
  Upstream::TypedLoadBalancerFactory& endpoint_picking_policy_factory_;
  const LoadBalancerConfigSharedPtr endpoint_picking_policy_config_;
  const std::chrono::milliseconds weight_update_period_;
  const double utilization_variance_threshold_;
  const double ewma_alpha_;
  const double remote_probe_fraction_;
  const std::chrono::milliseconds weight_expiration_period_;
  const std::vector<std::string> metric_names_for_computing_utilization_;
  Event::Dispatcher& main_thread_dispatcher_;
  ThreadLocal::SlotAllocator& tls_slot_allocator_;
};

// Key locality state by identity rather than HostsPerLocality position so add/remove index shifts
// cannot map main-thread snapshots to the wrong worker-local membership. Named to avoid shadowing
// Upstream::LocalityWeightsMap, which is a different type.
using LocalityRoutingWeightsMap =
    absl::flat_hash_map<envoy::config::core::v3::Locality, double, Upstream::LocalityHash,
                        Upstream::LocalityEqualTo>;

// EWMA state uses the same identity key: present-with-no-valid-hosts is stale; absent is cold.
using LocalityEwmaMap = LocalityRoutingWeightsMap;

struct PriorityRoutingWeights {
  enum class SelectionSource : uint8_t { Healthy = 0, Degraded = 1, AllHosts = 2 };
  static constexpr size_t kSourceCount = 3;

  struct SourceWeights {
    LocalityRoutingWeightsMap weights;
    // Does not affect the routing decision (fully encoded in weights); read on the hot path only to
    // pick the zone-routing stat counter.
    bool all_local{false};
  };

  std::array<SourceWeights, kSourceCount> by_source;

  const LocalityRoutingWeightsMap& weightsFor(SelectionSource s) const {
    return by_source[static_cast<size_t>(s)].weights;
  }
  bool allLocalFor(SelectionSource s) const { return by_source[static_cast<size_t>(s)].all_local; }
};

// Advisory per-priority locality weights. Priority/health/panic selection stays live on workers.
struct RoutingWeightsSnapshot {
  std::vector<PriorityRoutingWeights> priority_weights;
};

using RoutingWeightsSnapshotConstSharedPtr = std::shared_ptr<const RoutingWeightsSnapshot>;

struct ThreadLocalShim : public ThreadLocal::ThreadLocalObject {
  RoutingWeightsSnapshotConstSharedPtr routing_weights;
};

class WorkerLocalLb;

// Factory shared across workers; publishes routing-weight snapshots via TLS.
class WorkerLocalLbFactory : public Upstream::LoadBalancerFactory {
public:
  WorkerLocalLbFactory(Upstream::LoadBalancerFactorySharedPtr child_worker_factory,
                       LoadBalancerConfigSharedPtr child_config,
                       const Upstream::ClusterInfo& cluster_info, Runtime::Loader& runtime,
                       Envoy::Random::RandomGenerator& random,
                       ThreadLocal::SlotAllocator& tls_slot_allocator);

  // Upstream::LoadBalancerFactory
  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override;
  bool recreateOnHostChangeDeprecated() const override { return false; }

  void updateRoutingWeights(RoutingWeightsSnapshotConstSharedPtr snapshot) {
    tls_->runOnAllThreads([snapshot = std::move(snapshot)](OptRef<ThreadLocalShim> shim) {
      if (shim.has_value()) {
        shim->routing_weights = snapshot;
      }
    });
  }

  // SAFETY: Must only be called on a thread that owns a TLS slot instance (worker or main
  // thread). The shim object is created once per thread at factory construction and never
  // replaced (updateRoutingWeights mutates its field), and the factory outlives any worker LB it
  // created, so callers on their own thread may cache the returned pointer for their lifetime.
  const ThreadLocalShim* tlsShim() const {
    auto shim = tls_->get();
    return shim.ptr();
  }

  Upstream::LoadBalancerPtr
  createWorkerChildLb(Upstream::PrioritySetImpl& per_locality_priority_set);

  // Whether the child policy requires the worker LB to be recreated on host membership changes.
  bool recreateChildOnHostChange() const;

  Envoy::Random::RandomGenerator& random() const { return random_; }
  Runtime::Loader& runtime() const { return runtime_; }
  uint32_t healthyPanicThreshold() const { return healthy_panic_threshold_; }
  Upstream::ClusterLbStats& lbStats() const { return cluster_info_.lbStats(); }

private:
  // Worker factory of the child ThreadAwareLoadBalancer (owned by the main-thread LB). Shared
  // ownership keeps it valid on workers after the child LB is destroyed, per the
  // ThreadAwareLoadBalancer factory contract.
  Upstream::LoadBalancerFactorySharedPtr child_worker_factory_;
  // Co-owns the child endpoint-picking config so it outlives the child LB on worker threads, even
  // when the LoadAwareLocalityLbConfig that also owns it is destroyed first.
  LoadBalancerConfigSharedPtr child_config_;
  const Upstream::ClusterInfo& cluster_info_;
  Envoy::Random::RandomGenerator& random_;
  Runtime::Loader& runtime_;
  uint32_t healthy_panic_threshold_;

  std::unique_ptr<ThreadLocal::TypedSlot<ThreadLocalShim>> tls_;
};

} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
