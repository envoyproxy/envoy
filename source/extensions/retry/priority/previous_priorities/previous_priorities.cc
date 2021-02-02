#include "extensions/retry/priority/previous_priorities/previous_priorities.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

const Upstream::HealthyAndDegradedLoad& PreviousPrioritiesRetryPriority::determinePriorityLoad(
    const Upstream::PrioritySet& priority_set,
    const Upstream::HealthyAndDegradedLoad& original_priority_load,
    const PriorityMappingFunc& priority_mapping_func) {
  // If we've not seen enough retries to modify the priority load, just
  // return the original.
  // If this retry should trigger an update, recalculate the priority load by excluding attempted
  // priorities.
  if (attempted_hosts_.size() < update_frequency_) {
    return original_priority_load;
  } else if (attempted_hosts_.size() % update_frequency_ == 0) {
    if (excluded_priorities_.size() < priority_set.hostSetsPerPriority().size()) {
      excluded_priorities_.resize(priority_set.hostSetsPerPriority().size());
    }

    for (const auto& host : attempted_hosts_) {
      absl::optional<uint32_t> mapped_host_priority = priority_mapping_func(*host);
      if (mapped_host_priority.has_value()) {
        excluded_priorities_[mapped_host_priority.value()] = true;
      }
    }

    if (!adjustForAttemptedPriorities(priority_set)) {
      return original_priority_load;
    }
  }

  return per_priority_load_;
}

bool PreviousPrioritiesRetryPriority::adjustForAttemptedPriorities(
    const Upstream::PrioritySet& priority_set) {
  for (auto& host_set : priority_set.hostSetsPerPriority()) {
    recalculatePerPriorityState(host_set->priority(), priority_set);
  }

  std::vector<uint32_t> adjusted_per_priority_health(per_priority_health_.get().size(), 0);
  std::vector<uint32_t> adjusted_per_priority_degraded(per_priority_degraded_.get().size(), 0);
  auto total_availability =
      adjustedAvailability(adjusted_per_priority_health, adjusted_per_priority_degraded);

  // If there are no available priorities left, we reset the attempted priorities and recompute the
  // adjusted availability.
  // This allows us to fall back to the unmodified priority load when we run out of priorities
  // instead of failing to route requests.
  if (total_availability == 0) {
    for (auto excluded_priority : excluded_priorities_) {
      excluded_priority = false;
    }
    attempted_hosts_.clear();
    total_availability =
        adjustedAvailability(adjusted_per_priority_health, adjusted_per_priority_degraded);
  }

  // If total availability is still zero at this point, it must mean that all clusters are
  // completely unavailable. If so, fall back to using the original priority loads. This maintains
  // whatever handling the default LB uses when all priorities are unavailable.
  if (total_availability == 0) {
    return false;
  }

  std::fill(per_priority_load_.healthy_priority_load_.get().begin(),
            per_priority_load_.healthy_priority_load_.get().end(), 0);
  std::fill(per_priority_load_.degraded_priority_load_.get().begin(),
            per_priority_load_.degraded_priority_load_.get().end(), 0);

  // TODO(snowp): This code is basically distributeLoad from load_balancer_impl.cc, should probably
  // reuse that.

  // We then adjust the load by rebalancing priorities with the adjusted availability values.
  size_t total_load = 100;
  // The outer loop is used to eliminate rounding errors: any remaining load will be assigned to the
  // first availability priority.
  while (total_load != 0) {
    for (size_t i = 0; i < adjusted_per_priority_health.size(); ++i) {
      // Now assign as much load as possible to the high priority levels and cease assigning load
      // when total_load runs out.
      const auto delta = std::min<uint32_t>(total_load, adjusted_per_priority_health[i] * 100 /
                                                            total_availability);
      per_priority_load_.healthy_priority_load_.get()[i] += delta;
      total_load -= delta;
    }

    for (size_t i = 0; i < adjusted_per_priority_degraded.size(); ++i) {
      // Now assign as much load as possible to the high priority levels and cease assigning load
      // when total_load runs out.
      const auto delta = std::min<uint32_t>(total_load, adjusted_per_priority_degraded[i] * 100 /
                                                            total_availability);
      per_priority_load_.degraded_priority_load_.get()[i] += delta;
      total_load -= delta;
    }
  }

  return true;
}

uint32_t PreviousPrioritiesRetryPriority::adjustedAvailability(
    std::vector<uint32_t>& adjusted_per_priority_health,
    std::vector<uint32_t>& adjusted_per_priority_degraded) const {
  // Create an adjusted view of the priorities, where attempted priorities are given a zero load.
  // Create an adjusted health view of the priorities, where attempted priorities are
  // given a zero weight.
  uint32_t total_availability = 0;

  ASSERT(per_priority_health_.get().size() == per_priority_degraded_.get().size());

  for (size_t i = 0; i < per_priority_health_.get().size(); ++i) {
    if (!excluded_priorities_[i]) {
      adjusted_per_priority_health[i] = per_priority_health_.get()[i];
      adjusted_per_priority_degraded[i] = per_priority_degraded_.get()[i];
      total_availability += per_priority_health_.get()[i];
      total_availability += per_priority_degraded_.get()[i];
    } else {
      adjusted_per_priority_health[i] = 0;
      adjusted_per_priority_degraded[i] = 0;
    }
  }

  return std::min(total_availability, 100u);
}

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
