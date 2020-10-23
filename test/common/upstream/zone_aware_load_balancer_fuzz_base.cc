#include "zone_aware_load_balancer_fuzz_base.h"

#include "test/mocks/upstream/host_set.h"

namespace Envoy {
namespace Upstream {

void ZoneAwareLoadBalancerFuzzBase::setupZoneAwareLoadBalancingSpecificLogic() {
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
      // Having 3 possible weights, 1, 2, and 3 to provide the state space at least some variation
      // in regards to weights, which do affect the load balancing algorithm Cap the amount of
      // weights at 3 for simplicity's sake
      host->weight(
          (random_bytestring_[index_of_random_bytestring_ % random_bytestring_.length()] % 3) + 1);
      ++index_of_random_bytestring_;
    }
  }
}

// TODO(zasweq): If moving to static hosts, must clear out weights afterward

} // namespace Upstream
} // namespace Envoy
