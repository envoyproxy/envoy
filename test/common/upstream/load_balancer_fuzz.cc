#include "load_balancer_fuzz.h"


namespace Envoy {
namespace Upstream {

void LoadBalancerFuzzBase::initializeAndReplay(test::common::upstream::LoadBalancerTestCase input) {
    //will call initialize, which will all be specific to that
    initialize(input);
    replay(input);
}

//So, these should be shared amongst all of the types. Since logically, we're just setting the mock priority set to have certain values, we're
//doing the same thing across all of them here
/*void LoadBalancerFuzzBase::addHostSet() { //TODO: Do I even need this? It only seems to get to tertiary, 0, 1 in most of the cases
    
}*/

//Perhaps change host set to a vector of vectors
//This clears the host set and starts fresh
void LoadBalancerFuzzBase::updateHealthFlagsForAHostSet(bool failover_host_set, uint32_t num_hosts, uint32_t num_healthy_hosts, uint32_t num_degraded_hosts, uint32_t num_excluded_hosts) {
    //if total hosts doesn't work perhaps make total hosts = to all 3 put together
    MockHostSet& host_set = *priority_set_.getMockHostSet(int(failover_host_set));
    host_set.hosts_.clear();
    host_set.healthy_hosts_.clear();
    host_set.degraded_hosts_.clear();
    host_set.excluded_hosts_.clear();
    for (uint32_t i = 0; i < num_hosts; ++i) {
        int port = 80 + i;
        host_set.hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:" + std::to_string(port)));
    }
    uint32_t i = 0;
    for (; i < num_healthy_hosts; ++i) {
      host_set.healthy_hosts_.push_back(host_set.hosts_[i]);
    }
    for (; i < (num_healthy_hosts + num_degraded_hosts); ++i) {
      host_set.degraded_hosts_.push_back(host_set.hosts_[i]);
    }

    for (; i < (num_healthy_hosts + num_degraded_hosts + num_excluded_hosts); ++i) {
      host_set.excluded_hosts_.push_back(host_set.hosts_[i]);
    }

    //host_set is a placeholder for however the fuzzer will determine which host set to use
    host_set.runCallbacks({}, {});
}

//These two have a lot of logic attached to them such as mocks, so you need to delegate these to specific logic per each specific load balancer.
//What needs to be taken as an argument here?
/*void LoadBalancerFuzzBase::prefetch() {
    //TODO: specifics to each one?
    load_balancer_->peekAnotherHost(nullptr);
}

void LoadBalancerFuzzBase::chooseHost() {
    load_balancer_->chooseHost(nullptr);
}*/

void LoadBalancerFuzzBase::replay(test::common::upstream::LoadBalancerTestCase input) {
    constexpr auto max_actions = 64;
  for (int i = 0; i < std::min(max_actions, input.actions().size()); ++i) {
    const auto& event = input.actions(i);
    ENVOY_LOG_MISC(trace, "Action: {}", event.DebugString());
    switch (event.action_selector_case()) {
        case test::common::upstream::LbAction::kUpdateHealthFlags: {
            updateHealthFlagsForAHostSet(event.update_health_flags().failover_host_set(), event.update_health_flags().num_healthy_hosts(), event.update_health_flags().num_degraded_hosts(), event.update_health_flags().num_excluded_hosts());
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

void RandomLoadBalancerFuzzTest::initialize(test::common::upstream::LoadBalancerTestCase input) {
    load_balancer_ = RandomLoadBalancer(priority_set_, nullptr, stats_, runtime_, random_,
                                               input.common_lb_config());
}

void RandomLoadBalancerFuzzTest::prefetch() {
    load_balancer_->peekAnotherHost(nullptr);
}

void RandomLoadBalancerFuzzTest::chooseHost() {
    load_balancer_->chooseHost(nullptr);
}

} //namespace Upstream
} //namespace Envoy
