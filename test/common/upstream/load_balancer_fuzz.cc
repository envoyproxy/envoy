#include "test/common/upstream/load_balancer_fuzz.h"

#include "test/common/upstream/utility.h"

namespace Envoy {
namespace Upstream {

// Anonymous namespace for helper functions
namespace {
std::vector<uint64_t>
constructByteVectorForRandom(test::common::upstream::LoadBalancerTestCase input) {
  std::vector<uint64_t> byteVector;
  for (int i = 0; i < input.bytestring_for_random_calls().size(); ++i) {
    byteVector.push_back(input.bytestring_for_random_calls(i));
  }
  return byteVector;
}
} // namespace

// Required because super class constructor has logic
LoadBalancerFuzzBase::LoadBalancerFuzzBase() : LoadBalancerFuzzTestBase() {}

void LoadBalancerFuzzBase::initializeFixedHostSets(uint32_t num_hosts_in_priority_set,
                                                   uint32_t num_hosts_in_failover_set) {
  // TODO: Cap on ports?
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

void LoadBalancerFuzzBase::initializeAndReplay(test::common::upstream::LoadBalancerTestCase input) {
  // TODO: Keep this random instantiation even in load balancers that don't call into it, or do they
  // all?
  random_.bytestring_ = constructByteVectorForRandom(input);
  try {
    initialize(input); // Initializes specific load balancers
  } catch (EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException: {}", e.what());
    return;
  }
  initializeFixedHostSets(input.num_hosts_in_priority_set(), input.num_hosts_in_failover_set());
  replay(input);
}

// So, these should be shared amongst all of the types. Since logically, we're just setting the mock
// priority set to have certain values, we're doing the same thing across all of them here
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

void LoadBalancerFuzzBase::replay(test::common::upstream::LoadBalancerTestCase input) {
  constexpr auto max_actions = 64;
  for (int i = 0; i < std::min(max_actions, input.actions().size()); ++i) {
    const auto& event = input.actions(i);
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

RandomLoadBalancerFuzzTest::RandomLoadBalancerFuzzTest() : LoadBalancerFuzzBase() {}

void RandomLoadBalancerFuzzTest::initialize(test::common::upstream::LoadBalancerTestCase input) {
  load_balancer_ = std::make_unique<RandomLoadBalancer>(priority_set_, nullptr, stats_, runtime_,
                                                        random_, input.common_lb_config());
}

// For random load balancing, a randomly generated uint64 gets moded against the hosts to choose
// from. This is not something an untrusted upstream can affect, and fuzzing must be deterministic,
// so the fuzzer generates a bytestring which represents the random calls.

// Logic specific for random load balancers
void RandomLoadBalancerFuzzTest::prefetch() {
  // random() calls are handled by fake random
  load_balancer_->peekAnotherHost(nullptr);
}

void RandomLoadBalancerFuzzTest::chooseHost() { load_balancer_->chooseHost(nullptr); }

} // namespace Upstream
} // namespace Envoy
