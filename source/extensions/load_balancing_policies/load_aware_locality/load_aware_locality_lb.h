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

} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
