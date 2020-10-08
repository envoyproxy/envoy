#include "test/common/upstream/load_balancer_fuzz_base.h"

#include "test/common/upstream/utility.h"

namespace Envoy {
namespace Upstream {

void LoadBalancerFuzzBase::initializeFixedHostSets(uint32_t num_hosts_in_priority_set,
                                                   uint32_t num_hosts_in_failover_set) {
  int port = 80;
  for (uint32_t i = 0; i < num_hosts_in_priority_set; ++i) {
    host_set_.hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:" + std::to_string(port)));
    ++port;
  }
  for (uint32_t i = 0; i < num_hosts_in_failover_set; ++i) {
    failover_host_set_.hosts_.push_back(
        makeTestHost(info_, "tcp://127.0.0.1:" + std::to_string(port)));
    ++port;
  }
  // TODO(zasweq): More than two host sets?
}

// Initializes random and fixed host sets
void LoadBalancerFuzzBase::initializeLbComponents(
    const test::common::upstream::LoadBalancerTestCase& input) {
  random_.initialize(input.seed_for_prng());
  initializeFixedHostSets(input.num_hosts_in_priority_set(), input.num_hosts_in_failover_set());
}

//TODO: Mod it against the remaining hosts, have two sets where you take index out of one and put it in the other, or even just a set of indexes that you remove from
void LoadBalancerFuzzBase::updateHealthFlagsForAHostSet(bool failover_host_set,
                                                        uint32_t num_healthy_hosts,
                                                        uint32_t num_degraded_hosts,
                                                        uint32_t num_excluded_hosts) {
  MockHostSet& host_set = *priority_set_.getMockHostSet(int(failover_host_set));
  // This downcast will not overflow because size is capped by port numbers
  uint32_t host_set_size = host_set.hosts_.size();
  host_set.healthy_hosts_.clear();
  host_set.degraded_hosts_.clear();
  host_set.excluded_hosts_.clear();
  uint32_t i = 0;
  //construct a vector here representing remaining indexes, an index will randomly get chosen from here every time and removed. Thus, this represents the remaining indexes
  //When this goes to zero, there will be no more indexes left to place into healthy, degraded, and excluded hosts.
  std::vector<uint8_t> indexVector;
  byteVector.reserve(host_set_size);
  for (uint8_t i = 0; i < host_set_size; i++) {
    indexVector.push_back(i);
  }
  
  //Handle healthy hosts
  for (uint32_t i = 0; i < num_healthy_hosts && indexVector.size() != 0; i++) {
    uint64_t index = random_.random() % indexVector.size(); //does this size() return a uint64_t? also, should I add logs here?
    host_set.healthy_hosts_.push_back(host_set.hosts_[index]);
  }

  //Handle degraded hosts
  for (uint32_t i = 0; i < num_degraded_hosts && indexVector.size() != 0; i++) {
    uint64_t index = random_.random() % indexVector.size(); //does this size() return a uint64_t?
    host_set.degraded_hosts_.push_back(host_set.hosts_[index]);
  }

  //Handle excluded hosts
  for (uint32_t i = 0; i < num_excluded_hosts && indexVector.size() != 0; i++) {
    uint64_t index = random_.random() % indexVector.size(); //does this size() return a uint64_t?
    host_set.excluded_hosts_.push_back(host_set.hosts_[index]);
  }

  host_set.runCallbacks({}, {});
}

// Updating host sets is shared amongst all the load balancer tests. Since logically, we're just
// setting the mock priority set to have certain values, and all load balancers interface with host
// sets and their health statuses, this action maps to all load balancers.
void LoadBalancerFuzzBase::updateHealthFlagsForAHostSet(bool failover_host_set,
                                                        uint32_t num_healthy_hosts,
                                                        uint32_t num_degraded_hosts,
                                                        uint32_t num_excluded_hosts) {
  MockHostSet& host_set = *priority_set_.getMockHostSet(int(failover_host_set));
  // Will not overflow because size is capped by port numbers
  uint32_t host_set_size = host_set.hosts_.size();
  //TODO: Clear vectors?
  uint32_t i = 0;
  for (; i < std::min(num_healthy_hosts, host_set_size); ++i) {
    host_set.healthy_hosts_.push_back(host_set.hosts_[i]); //TODO: Preallocate space in this vector
  }
  for (; i < std::min(num_healthy_hosts + num_degraded_hosts, host_set_size); ++i) {
    host_set.degraded_hosts_.push_back(host_set.hosts_[i]);
  }

  for (; i < std::min(num_healthy_hosts + num_degraded_hosts + num_excluded_hosts, host_set_size);
       ++i) {
    host_set.excluded_hosts_.push_back(host_set.hosts_[i]);
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
      updateHealthFlagsForAHostSet(event.update_health_flags().failover_host_set(),
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
