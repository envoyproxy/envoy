#include "test/common/upstream/load_balancer_fuzz_base.h"

#include "test/common/upstream/utility.h"

namespace Envoy {
namespace Upstream {

void LoadBalancerFuzzBase::initializeASingleHostSet(uint32_t num_hosts_in_host_set,
                                                    uint8_t index_of_host_set) {
  ENVOY_LOG_MISC(trace, "Will attempt to initialize host set {} with {} hosts.", index_of_host_set,
                 num_hosts_in_host_set);
  MockHostSet& host_set = *priority_set_.getMockHostSet(index_of_host_set);
  uint32_t hosts_made = 0;
  // Cap each host set at 250 hosts for efficiency
  uint32_t max_num_hosts_in_host_set = 250;
  // Leave port clause in for future changes
  while (hosts_made < std::min(num_hosts_in_host_set, max_num_hosts_in_host_set) && port_ < 65535) {
    host_set.hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:" + std::to_string(port_)));
    ++port_;
    ++hosts_made;
  }
  // TODO: Random call and mod against host_set size for locality logic
}

// Initializes random and fixed host sets
void LoadBalancerFuzzBase::initializeLbComponents(
    const test::common::upstream::LoadBalancerTestCase& input) {
  random_.initialize(input.seed_for_prng());
  uint8_t index_of_host_set = 0;
  for (uint16_t num_hosts_in_host_set : input.num_hosts_in_host_set()) {
    initializeASingleHostSet(num_hosts_in_host_set, index_of_host_set);
    index_of_host_set++;
  }
  num_host_sets_ = index_of_host_set;
}

// Updating host sets is shared amongst all the load balancer tests. Since logically, we're just
// setting the mock priority set to have certain values, and all load balancers interface with host
// sets and their health statuses, this action maps to all load balancers.
void LoadBalancerFuzzBase::updateHealthFlagsForAHostSet(uint64_t host_index,
                                                        uint32_t num_healthy_hosts,
                                                        uint32_t num_degraded_hosts,
                                                        uint32_t num_excluded_hosts) {
  uint8_t index_of_host_set = host_index % num_host_sets_;
  ENVOY_LOG_MISC(trace, "Updating health flags for host set: {}", index_of_host_set);
  MockHostSet& host_set = *priority_set_.getMockHostSet(index_of_host_set);
  // This downcast will not overflow because size is capped by port numbers
  uint32_t host_set_size = host_set.hosts_.size();
  host_set.healthy_hosts_.clear();
  host_set.degraded_hosts_.clear();
  host_set.excluded_hosts_.clear();
  // The vector constructed here represents remaining indexes, an index will randomly get chosen
  // from here every time and removed. Thus, this represents the remaining indexes. When this goes
  // to zero, there will be no more indexes left to place into healthy, degraded, and excluded
  // hosts.
  ENVOY_LOG_MISC(trace, "Host set size: {}", host_set_size);
  std::vector<uint8_t> index_vector;
  index_vector.reserve(host_set_size);
  for (uint8_t i = 0; i < host_set_size; i++) {
    index_vector.push_back(i);
  }

  for (uint32_t i = 0; i < num_healthy_hosts && index_vector.size() != 0; i++) {
    uint64_t index_of_index_vector = random_.random() % index_vector.size();
    uint64_t index = index_vector.at(index_of_index_vector);
    ENVOY_LOG_MISC(trace, "Added host {} to healthy hosts for host set {}", index,
                   index_of_host_set);
    host_set.healthy_hosts_.push_back(host_set.hosts_[index]);
    index_vector.erase(index_vector.begin() + index_of_index_vector);
  }

  for (uint32_t i = 0; i < num_degraded_hosts && index_vector.size() != 0; i++) {
    uint64_t index_of_index_vector = random_.random() % index_vector.size();
    uint64_t index = index_vector.at(index_of_index_vector);
    ENVOY_LOG_MISC(trace, "Added host {} to degraded hosts for host set {}", index,
                   index_of_host_set);
    host_set.degraded_hosts_.push_back(host_set.hosts_[index]);
    index_vector.erase(index_vector.begin() + index_of_index_vector);
  }

  for (uint32_t i = 0; i < num_excluded_hosts && index_vector.size() != 0; i++) {
    uint64_t index_of_index_vector = random_.random() % index_vector.size();
    uint64_t index = index_vector.at(index_of_index_vector);
    ENVOY_LOG_MISC(trace, "Added host {} to excluded hosts for host set {}", index,
                   index_of_host_set);
    host_set.excluded_hosts_.push_back(host_set.hosts_[index]);
    index_vector.erase(index_vector.begin() + index_of_index_vector);
  }

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
      updateHealthFlagsForAHostSet(event.update_health_flags().host_index(),
                                   event.update_health_flags().num_healthy_hosts(),
                                   event.update_health_flags().num_degraded_hosts(),
                                   event.update_health_flags().num_excluded_hosts());
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
