#pragma once

#include "envoy/upstream/retry.h"

#include "common/upstream/load_balancer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

class OtherPriorityRetryPriority : public Upstream::RetryPriority {
public:
  OtherPriorityRetryPriority(uint32_t update_frequency) : update_frequency_(update_frequency) {}

  const Upstream::PriorityLoad&
  determinePriorityLoad(const Upstream::PrioritySet& priority_set,
                        const Upstream::PriorityLoad& original_priority) override;

  void onHostAttempted(Upstream::HostDescriptionConstSharedPtr attempted_host) override {
    attempted_priorites_.emplace_back(attempted_host->priority());
  }

private:
  void recalculatePerPriorityState(uint32_t priority, const Upstream::PrioritySet& priority_set) {
    // Recalcuate health and priority the same way the load balancer does it.
    Upstream::LoadBalancerBase::recalculatePerPriorityState(
        priority, priority_set, per_priority_load_, per_priority_health_);
  }

  // Distributes priority load between priorities that should be considered after
  // excluding attempted priorities.
  void adjustForAttemptedPriorities();

  const uint32_t update_frequency_;
  std::vector<uint32_t> attempted_priorites_;
  std::vector<bool> excluded_priorities_;
  Upstream::PriorityLoad per_priority_load_;
  std::vector<uint32_t> per_priority_health_;
  bool initialized_{};
};

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
