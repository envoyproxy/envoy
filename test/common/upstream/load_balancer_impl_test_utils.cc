// class LoadBalancerTestBase

// hostSet() returns a host_set_, constructor generates stats and puts it into a stats variable

// class TestLb: public LoadBalancerBase, essentiallly an override of the sources load balancer
// This is essentially overriding the source code load balancer, bringing in some methods from it,
// and overriding methods that wont be used

// class LoadBalancerBaseTest: public LoadBalancerTestBase

// Util function for updating host sets, config, uses test load balancer, and common config

// class RoundRobinLoadBalancerTest: public LoadBalancerTestBase

// init, updateHosts, shared_ptr localPriority sets etc., this might take a while

// class LeastRequestLoadBalancerTest: public LoadBalancerTestBase

// all this has is a public load balancer that is typed as a "LeastRequestLoadBalancer"

// class RandomLoadBalancerTest: public LoadBalancerTestBase

// Same thing as least request load balancer, except this one is a random load balancer

// TODO today, scope out how long you think each part of the fuzzer is going to take you, Address
// techincal debt from Adi's comments

namespace Envoy {
namespace Upstream {
namespace Random {
  FakeRandomGenerator::FakeRandomGenerator() {
    
  }
  uint64_t FakeRandomGenerator::random() {
    uint8_t index_of_data = counter % bytestring_.size();
    ++counter;
    return bytestring.at(index_of_data);
  }

}
} // namespace
} // namespace Upstream
} // namespace Envoy
