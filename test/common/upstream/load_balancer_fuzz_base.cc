#include "test/common/upstream/load_balancer_fuzz_base.h"

#include "test/common/upstream/utility.h"

namespace Envoy {

namespace Random {
uint64_t FakeRandomGenerator::random() {
  uint8_t index_of_data = counter % bytestring_.size();
  ++counter;
  ENVOY_LOG_MISC(trace, "random() returned: {}", bytestring_.at(index_of_data));
  return bytestring_.at(index_of_data);
}
} // namespace Random

namespace Upstream {

// Anonymous namespace for helper functions
namespace {
std::vector<uint64_t>
constructByteVectorForRandom(const Protobuf::RepeatedField<uint64_t>& byteString) {
  std::vector<uint64_t> byteVector;
  for (int i = 0; i < byteString.size(); ++i) {
    byteVector.push_back(byteString.at(i));
  }
  return byteVector;
}
} // namespace

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
  // TODO: More than two hosts?
}

// Initializes random and fixed host sets
void LoadBalancerFuzzBase::initializeLbComponents(
    test::common::upstream::LoadBalancerTestCase input) {
  random_.bytestring_ = constructByteVectorForRandom(input.bytestring_for_random_calls());
  initializeFixedHostSets(input.num_hosts_in_priority_set(), input.num_hosts_in_failover_set());
}

// Updating host sets is shared amongst all the load balancer tests. Since logically, we're just
// setting the mock priority set to have certain values, and all load balancers interface with host
// sets and their health statuses, this action maps to all load balancers.
void LoadBalancerFuzzBase::updateHealthFlagsForAHostSet(bool failover_host_set,
                                                        uint32_t num_healthy_hosts,
                                                        uint32_t num_degraded_hosts,
                                                        uint32_t num_excluded_hosts) {
  MockHostSet& host_set = *priority_set_.getMockHostSet(int(failover_host_set));
  uint32_t i = 0;
  for (; i < num_healthy_hosts && i < host_set.hosts_.size(); ++i) {
    host_set.healthy_hosts_.push_back(host_set.hosts_[i]);
  }
  for (; i < (num_healthy_hosts + num_degraded_hosts) && i < host_set.hosts_.size(); ++i) {
    host_set.degraded_hosts_.push_back(host_set.hosts_[i]);
  }

  for (; i < (num_healthy_hosts + num_degraded_hosts + num_excluded_hosts) &&
         i < host_set.hosts_.size();
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
                                   event.update_health_flags().num_degraded_hosts(),
                                   event.update_health_flags().num_excluded_hosts());
      break;
    }
    case test::common::upstream::LbAction::kPrefetch: {
      prefetch();
      break;
    }
    case test::common::upstream::LbAction::kChooseHost: {
      chooseHost();
      break;
    }
    default:
      break;
    }
  }
}

} // namespace Upstream
} // namespace Envoy
