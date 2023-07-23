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
  if (priority_level == 0 && local_priority_set_) {
    // TODO(zasweq): Perhaps fuzz the local priority set as a distinct host set? rather than
    // making it P = 0 of main Priority Set
    const MockHostSet& host_set = *priority_set_.getMockHostSet(priority_level);
    const HostVector empty_host_vector;
    local_priority_set_->updateHosts(0, HostSetImpl::updateHostsParams(host_set), {},
                                     empty_host_vector, empty_host_vector, absl::nullopt);
  }
}

void ZoneAwareLoadBalancerFuzzBase::updateHealthFlagsForAHostSet(
    const uint64_t host_priority, const uint32_t num_healthy_hosts,
    const uint32_t num_degraded_hosts, const uint32_t num_excluded_hosts,
    const Protobuf::RepeatedField<Protobuf::uint32>& random_bytestring) {
  LoadBalancerFuzzBase::updateHealthFlagsForAHostSet(
      host_priority, num_healthy_hosts, num_degraded_hosts, num_excluded_hosts, random_bytestring);
  // Update local priority set if it exists - will mean load balancer is zone aware and has decided
  // to construct local priority set
  const uint8_t priority_of_host_set = host_priority % num_priority_levels_;
  if (priority_of_host_set == 0 && local_priority_set_) {
    const MockHostSet& host_set = *priority_set_.getMockHostSet(priority_of_host_set);
    const HostVector empty_host_vector;
    local_priority_set_->updateHosts(0, HostSetImpl::updateHostsParams(host_set), {},
                                     empty_host_vector, empty_host_vector, absl::nullopt);
  }
}

void ZoneAwareLoadBalancerFuzzBase::initializeLbComponents(
    const test::common::upstream::LoadBalancerTestCase& input) {
  LoadBalancerFuzzBase::initializeLbComponents(input);
  setupZoneAwareLoadBalancingSpecificLogic();
}

void ZoneAwareLoadBalancerFuzzBase::setupZoneAwareLoadBalancingSpecificLogic() {
  // Having 3 possible weights, 1, 2, and 3 to provide the state space at least some variation
  // in regards to weights, which do affect the load balancing algorithm. Cap the amount of
  // weights at 3 for simplicity's sake
  addWeightsToHosts();
}

// Initialize the host set with weights once at setup
void ZoneAwareLoadBalancerFuzzBase::addWeightsToHosts() {
  // Iterate through all the current host sets and update weights for each
  for (uint32_t priority_level = 0; priority_level < priority_set_.hostSetsPerPriority().size();
       ++priority_level) {
    MockHostSet& host_set = *priority_set_.getMockHostSet(priority_level);
    for (auto& host : host_set.hosts_) {
      // Make sure no weights persisted from previous fuzz iterations
      ASSERT(host->weight() == 1);
      host->weight(
          (random_bytestring_[index_of_random_bytestring_ % random_bytestring_.length()] % 3) + 1);
      ++index_of_random_bytestring_;
    }
  }
}

} // namespace Upstream
} // namespace Envoy
