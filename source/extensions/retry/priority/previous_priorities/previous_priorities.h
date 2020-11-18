#pragma once

#include "envoy/upstream/retry.h"

#include "common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

class PreviousPrioritiesRetryPriority : public Upstream::RetryPriority {
public:
  PreviousPrioritiesRetryPriority(uint32_t update_frequency, uint32_t max_retries)
      : update_frequency_(update_frequency) {
    attempted_hosts_.reserve(max_retries);
  }

  const Upstream::HealthyAndDegradedLoad&
  determinePriorityLoad(const Upstream::PrioritySet& priority_set,
                        const Upstream::HealthyAndDegradedLoad& original_priority_load,
                        const PriorityMappingFunc& priority_mapping_func) override;

  void onHostAttempted(Upstream::HostDescriptionConstSharedPtr attempted_host) override {
    attempted_hosts_.emplace_back(attempted_host);
  }

private:
  void recalculatePerPriorityState(uint32_t priority, const Upstream::PrioritySet& priority_set) {
    // Recalculate health and priority the same way the load balancer does it.
    Upstream::LoadBalancerBase::recalculatePerPriorityState(
        priority, priority_set, per_priority_load_, per_priority_health_, per_priority_degraded_);
  }

  uint32_t adjustedAvailability(std::vector<uint32_t>& per_priority_health,
                                std::vector<uint32_t>& per_priority_degraded) const;

  // Distributes priority load between priorities that should be considered after
  // excluding attempted priorities.
  // @return whether the adjustment was successful. If not, the original priority load should be
  // used.
  bool adjustForAttemptedPriorities(const Upstream::PrioritySet& priority_set);

  const uint32_t update_frequency_;
  std::vector<Upstream::HostDescriptionConstSharedPtr> attempted_hosts_;
  std::vector<bool> excluded_priorities_;
  Upstream::HealthyAndDegradedLoad per_priority_load_;
  Upstream::HealthyAvailability per_priority_health_;
  Upstream::DegradedAvailability per_priority_degraded_;
};

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
