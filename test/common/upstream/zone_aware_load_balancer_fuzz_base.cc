#include "zone_aware_load_balancer_fuzz_base.h"

#include "test/mocks/upstream/host_set.h"

namespace Envoy {
namespace Upstream {

void ZoneAwareLoadBalancerFuzzBase::initializeASingleHostSet(
    const test::common::upstream::SetupPriorityLevel& setup_priority_level,
    const uint8_t priority_level, uint16_t& port) {
  LoadBalancerFuzzBase::initializeASingleHostSet(setup_priority_level, priority_level, port);
  // Update local priority set if it exists - will mean load balancer is zone aware and has decided
  // to construct local priority set
  if (priority_level == 0 && local_priority_set_.get() != nullptr) {
    MockHostSet& host_set = *priority_set_.getMockHostSet(priority_level);
    local_priority_set_->getMockHostSet(0)->hosts_ = host_set.hosts_;
    local_priority_set_->getMockHostSet(0)->hosts_per_locality_ = host_set.hosts_per_locality_;
  }
}

void ZoneAwareLoadBalancerFuzzBase::updateHealthFlagsForAHostSet(
    const uint64_t host_priority, const uint32_t num_healthy_hosts,
    const uint32_t num_degraded_hosts, const uint32_t num_excluded_hosts,
    const std::string random_bytestring) {
  LoadBalancerFuzzBase::updateHealthFlagsForAHostSet(
      host_priority, num_healthy_hosts, num_degraded_hosts, num_excluded_hosts, random_bytestring);
  // Update local priority set if it exists - will mean load balancer is zone aware and has decided
  // to construct local priority set
  const uint8_t priority_of_host_set = host_priority % num_priority_levels_;
  if (priority_of_host_set == 0 && local_priority_set_.get() != nullptr) {
    MockHostSet& host_set = *priority_set_.getMockHostSet(priority_of_host_set);
    local_priority_set_->getMockHostSet(0)->healthy_hosts_ = host_set.healthy_hosts_;
    local_priority_set_->getMockHostSet(0)->degraded_hosts_ = host_set.degraded_hosts_;
    local_priority_set_->getMockHostSet(0)->excluded_hosts_ = host_set.excluded_hosts_;
    local_priority_set_->getMockHostSet(0)->healthy_hosts_per_locality_ =
        host_set.healthy_hosts_per_locality_;
    local_priority_set_->getMockHostSet(0)->degraded_hosts_per_locality_ =
        host_set.degraded_hosts_per_locality_;
    local_priority_set_->getMockHostSet(0)->excluded_hosts_per_locality_ =
        host_set.excluded_hosts_per_locality_;
    local_priority_set_->getMockHostSet(0)->runCallbacks({}, {});
  }
}

void ZoneAwareLoadBalancerFuzzBase::setupZoneAwareLoadBalancingSpecificLogic() {
  // Having 3 possible weights, 1, 2, and 3 to provide the state space at least some variation
  // in regards to weights, which do affect the load balancing algorithm Cap the amount of
  // weights at 3 for simplicity's sake
  stats_.max_host_weight_.set(3UL);
  addWeightsToHosts();
}

// Initialize the host set with weights once at setup
void ZoneAwareLoadBalancerFuzzBase::addWeightsToHosts() {
  // Iterate through all the current host sets and update weights for each
  for (uint32_t priority_level = 0; priority_level < priority_set_.hostSetsPerPriority().size();
       ++priority_level) {
    MockHostSet& host_set = *priority_set_.getMockHostSet(priority_level);
    for (auto& host : host_set.hosts_) {
      host->weight(
          (random_bytestring_[index_of_random_bytestring_ % random_bytestring_.length()] % 3) + 1);
      ++index_of_random_bytestring_;
    }
  }
}

// TODO(zasweq): If moving to static hosts, must clear out weights afterward

} // namespace Upstream
} // namespace Envoy
