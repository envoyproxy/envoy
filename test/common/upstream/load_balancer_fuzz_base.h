#include "envoy/config/cluster/v3/cluster.pb.h"

#include <random>

#include "common/upstream/load_balancer_impl.h"

#include "test/common/upstream/load_balancer_fuzz.pb.validate.h"
#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"

namespace Envoy {

namespace Random {
class FakeRandomGenerator : public RandomGenerator {
public:
  FakeRandomGenerator() = default;
  ~FakeRandomGenerator() override = default;

  void initialize(uint64_t seed) {
    prng_ = std::make_unique<std::mt19937_64>(seed);
  }

  // RandomGenerator
  uint64_t random() override {
    uint64_t toReturn = (*prng_.get())(); //For logging purposes
    ENVOY_LOG_MISC(trace, "random() returned: {}", toReturn);
    return toReturn;
  }
  std::string uuid() override { return ""; }
  std::unique_ptr<std::mt19937_64> prng_; 
};
} // namespace Random

namespace Upstream {

// This class implements replay logic, and also handles the initial setup of static host sets and
// the subsequent updates to those sets.
class LoadBalancerFuzzBase {
public:
  LoadBalancerFuzzBase() : stats_(ClusterInfoImpl::generateStats(stats_store_)){};

  // Untrusted upstreams don't have the ability to change the host set size, so keep it constant
  // over the fuzz iteration.
  void initializeFixedHostSets(uint32_t num_hosts_in_priority_set,
                               uint32_t num_hosts_in_failover_set);

  // Initializes load balancer components shared amongst every load balancer, random_, and
  // priority_set_
  void initializeLbComponents(const test::common::upstream::LoadBalancerTestCase& input);
  void updateHealthFlagsForAHostSet(bool failover_host_set, uint32_t num_healthy_hosts,
                                    uint32_t num_degraded_hosts, uint32_t num_excluded_hosts);
  // These two actions have a lot of logic attached to them. However, all the logic that the load
  // balancer needs to run its algorithm is already encapsulated within the load balancer. Thus,
  // once the load balancer is constructed, all this class has to do is call lb_->peekAnotherHost()
  // and lb_->chooseHost().
  void prefetch();
  void chooseHost();
  ~LoadBalancerFuzzBase() = default;
  void replay(const Protobuf::RepeatedPtrField<test::common::upstream::LbAction>& actions);

  // These public objects shared amongst all types of load balancers will be used to construct load
  // balancers in specific load balancer fuzz classes
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  Random::FakeRandomGenerator random_;
  NiceMock<MockPrioritySet> priority_set_;
  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  MockHostSet& failover_host_set_ = *priority_set_.getMockHostSet(1);
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  std::unique_ptr<LoadBalancerBase> lb_;
};

} // namespace Upstream
} // namespace Envoy
