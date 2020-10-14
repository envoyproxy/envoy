#include "test/common/upstream/load_balancer_fuzz_base.h"

#include "test/common/upstream/utility.h"

namespace Envoy {
namespace Upstream {

namespace {

constexpr uint32_t MaxNumHostsPerPriorityLevel = 256;

} // namespace

void LoadBalancerFuzzBase::initializeASingleHostSet(
    const test::common::upstream::SetupPriorityLevel& setup_priority_level,
    const uint8_t priority_level) {
  const uint32_t num_hosts_in_priority_level = setup_priority_level.num_hosts_in_priority_level();
  ENVOY_LOG_MISC(trace, "Will attempt to initialize host set {} with {} hosts.", priority_level,
                 num_hosts_in_priority_level);
  MockHostSet& host_set = *priority_set_.getMockHostSet(priority_level);
  uint32_t hosts_made = 0;
  // Cap each host set at 256 hosts for efficiency
  const uint32_t max_num_hosts_in_priority_level = MaxNumHostsPerPriorityLevel;
  // Leave port clause in for future changes
  while (hosts_made < std::min(num_hosts_in_priority_level, max_num_hosts_in_priority_level) &&
         port_ < 65535) {
    host_set.hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:" + std::to_string(port_)));
    ++port_;
    ++hosts_made;
  }

  Fuzz::ProperSubsetSelector subset_selector(setup_priority_level.random_bytestring());

  const std::vector<std::vector<uint8_t>> localities = subset_selector.constructSubsets(
      {setup_priority_level.num_hosts_locality_one(), setup_priority_level.num_hosts_locality_two(),
       setup_priority_level.num_hosts_locality_three()},
      std::min(num_hosts_in_priority_level, max_num_hosts_in_priority_level));

  // Construct three vectors of hosts each representing a locality level, construct
  // hosts_per_locality from these three vectors
  HostVector locality_one = {};
  for (uint8_t index : localities[0]) {
    ENVOY_LOG_MISC(trace, "Added host at index {} to locality 1", index);
    locality_one.push_back(host_set.hosts_[index]);
    locality_indexes_[index] = 0;
  }

  HostVector locality_two = {};
  for (uint8_t index : localities[1]) {
    ENVOY_LOG_MISC(trace, "Added host at index {} to locality 2", index);
    locality_two.push_back(host_set.hosts_[index]);
    locality_indexes_[index] = 1;
  }

  HostVector locality_three = {};
  for (uint8_t index : localities[2]) {
    ENVOY_LOG_MISC(trace, "Added host at index {} to locality 3", index);
    locality_three.push_back(host_set.hosts_[index]);
    locality_indexes_[index] = 2;
  }

  host_set.hosts_per_locality_ = makeHostsPerLocality({locality_one, locality_two, locality_three});
}

// Initializes random and fixed host sets
void LoadBalancerFuzzBase::initializeLbComponents(
    const test::common::upstream::LoadBalancerTestCase& input) {
  random_.initializeSeed(input.seed_for_prng());
  uint8_t priority_of_host_set = 0;
  for (const auto& setup_priority_level : input.setup_priority_levels()) {
    initializeASingleHostSet(setup_priority_level, priority_of_host_set);
    priority_of_host_set++;
  }
  num_priority_levels_ = priority_of_host_set;
}

// Updating host sets is shared amongst all the load balancer tests. Since logically, we're just
// setting the mock priority set to have certain values, and all load balancers interface with host
// sets and their health statuses, this action maps to all load balancers.
void LoadBalancerFuzzBase::updateHealthFlagsForAHostSet(const uint64_t host_priority,
                                                        const uint32_t num_healthy_hosts,
                                                        const uint32_t num_degraded_hosts,
                                                        const uint32_t num_excluded_hosts,
                                                        const std::string random_bytestring) {
  const uint8_t priority_of_host_set = host_priority % num_priority_levels_;
  ENVOY_LOG_MISC(trace, "Updating health flags for host set at priority: {}", priority_of_host_set);
  MockHostSet& host_set = *priority_set_.getMockHostSet(priority_of_host_set);
  // This downcast will not overflow because size is capped by port numbers
  const uint32_t host_set_size = host_set.hosts_.size();
  host_set.healthy_hosts_.clear();
  host_set.degraded_hosts_.clear();
  host_set.excluded_hosts_.clear();

  Fuzz::ProperSubsetSelector subset_selector(random_bytestring);

  const std::vector<std::vector<uint8_t>> subsets = subset_selector.constructSubsets(
      {num_healthy_hosts, num_degraded_hosts, num_excluded_hosts}, host_set_size);

  // Healthy hosts are first subset
  for (uint8_t index : subsets.at(0)) {
    host_set.healthy_hosts_.push_back(host_set.hosts_[index]);
    ENVOY_LOG_MISC(trace, "Index of host made healthy at priority level {}: {}",
                   priority_of_host_set, index);
  }

  // Degraded hosts are second subset
  for (uint8_t index : subsets.at(1)) {
    host_set.degraded_hosts_.push_back(host_set.hosts_[index]);
    ENVOY_LOG_MISC(trace, "Index of host made degraded at priority level {}: {}",
                   priority_of_host_set, index);
  }

  // Excluded hosts are third subset
  for (uint8_t index : subsets.at(2)) {
    host_set.excluded_hosts_.push_back(host_set.hosts_[index]);
    ENVOY_LOG_MISC(trace, "Index of host made excluded at priority level {}: {}",
                   priority_of_host_set, index);
  }

  // Handle updating health flags for hosts_per_locality_

  // The index within the array of the vector represents the locality
  std::array<HostVector, 3> healthy_hosts_per_locality;
  std::array<HostVector, 3> degraded_hosts_per_locality;
  std::array<HostVector, 3> excluded_hosts_per_locality;

  // Wrap those three in an array here, where the index represents health flag of
  // healthy/degraded/excluded, used for indexing during iteration through subsets
  std::array<std::array<HostVector, 3>, 3> locality_health_flags = {
      healthy_hosts_per_locality, degraded_hosts_per_locality, excluded_hosts_per_locality};

  // Iterate through subsets
  for (uint8_t health_flag = 0; health_flag < locality_health_flags.size(); health_flag++) {
    for (uint8_t index : subsets.at(health_flag)) { // Each subset logically represents a health
                                                    // flag
      // If the host is in a locality, we have to update the corresponding health flag host vector
      if (!(locality_indexes_.find(index) == locality_indexes_.end())) {
        // First dimension of array represents health_flag, second represents locality, which is
        // pulled from map
        locality_health_flags[health_flag][locality_indexes_[index]].push_back(
            host_set.hosts_[index]);
        ENVOY_LOG_MISC(trace, "Added host at index {} in locality {} to health flag set {}", index,
                       locality_indexes_[index], health_flag + 1);
      }
    }
  }

  // This overrides what is currently present in the host set, thus not having to explicitly call
  // vector.clear()
  host_set.healthy_hosts_per_locality_ =
      makeHostsPerLocality({healthy_hosts_per_locality[0], healthy_hosts_per_locality[1],
                            healthy_hosts_per_locality[2]});
  host_set.degraded_hosts_per_locality_ =
      makeHostsPerLocality({degraded_hosts_per_locality[0], degraded_hosts_per_locality[1],
                            degraded_hosts_per_locality[2]});
  host_set.excluded_hosts_per_locality_ =
      makeHostsPerLocality({excluded_hosts_per_locality[0], excluded_hosts_per_locality[1],
                            excluded_hosts_per_locality[2]});

  host_set.runCallbacks({}, {});
}

void LoadBalancerFuzzBase::prefetch() {
  // TODO: context, could generate it in proto action
  lb_->peekAnotherHost(nullptr);
}

void LoadBalancerFuzzBase::chooseHost() {
  // TODO: context, could generate it in proto action
  lb_->chooseHost(nullptr);
}

void LoadBalancerFuzzBase::replay(
    const Protobuf::RepeatedPtrField<test::common::upstream::LbAction>& actions) {
  constexpr auto max_actions = 64;
  for (int i = 0; i < std::min(max_actions, actions.size()); ++i) {
    const auto& event = actions.at(i);
    ENVOY_LOG_MISC(trace, "Action: {}", event.DebugString());
    switch (event.action_selector_case()) {
    case test::common::upstream::LbAction::kUpdateHealthFlags: {
      updateHealthFlagsForAHostSet(event.update_health_flags().host_priority(),
                                   event.update_health_flags().num_healthy_hosts(),
                                   event.update_health_flags().num_degraded_hosts(),
                                   event.update_health_flags().num_excluded_hosts(),
                                   event.update_health_flags().random_bytestring());
      break;
    }
    case test::common::upstream::LbAction::kPrefetch:
      prefetch();
      break;
    case test::common::upstream::LbAction::kChooseHost:
      chooseHost();
      break;
    default:
      break;
    }
  }
}

} // namespace Upstream
} // namespace Envoy
