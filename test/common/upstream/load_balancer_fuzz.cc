#include "load_balancer_fuzz.h"


namespace Envoy {
namespace Upstream {

void LoadBalancerFuzzBase::initializeAndReplay(test::common::upstream::LoadBalancerTestCase input) {
    //will call initialize, which will all be specific to that
    initialize(input);
    replay(input);
}

void LoadBalancerFuzzBase::addHostSet() {
    
}

void LoadBalancerFuzzBase::updateHealthFlags() {

}

void LoadBalancerFuzzBase::prefetch() {
    //TODO: specifics to each one?
    load_balancer_->peekAnotherHost(nullptr);
}

void LoadBalancerFuzzBase::chooseHost() {
    load_balancer_->chooseHost(nullptr);
}

void LoadBalancerFuzzBase::replay(test::common::upstream::LoadBalancerTestCase input) {

}

void RandomLoadBalancerFuzzTest::initialize(test::common::upstream::LoadBalancerTestCase input) {
    load_balancer_ = RandomLoadBalancer(priority_set_, nullptr, stats_, runtime_, random_,
                                               input.common_config_);
}



} //namespace Upstream
} //namespace Envoy
