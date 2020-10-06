#include "common/upstream/load_balancer_impl.h"

#include "test/common/upstream/load_balancer_fuzz.pb.validate.h"
#include "test/common/upstream/load_balancer_fuzz_test_utils.h"

namespace Envoy {
namespace Upstream {

class LoadBalancerFuzzBase : public LoadBalancerFuzzTestBase {
public:
  LoadBalancerFuzzBase();

  // Untrusted upstreams don't have the ability to change the host set size, so keep it constant
  // over the fuzz iteration.
  void initializeFixedHostSets(uint32_t num_hosts_in_priority_set,
                               uint32_t num_hosts_in_failover_set);

  virtual void initialize(test::common::upstream::LoadBalancerTestCase input) PURE;
  void initializeAndReplay(test::common::upstream::LoadBalancerTestCase input);
  void updateHealthFlagsForAHostSet(bool failover_host_set, uint32_t num_healthy_hosts,
                                    uint32_t num_degraded_hosts = 0,
                                    uint32_t num_excluded_hosts = 0);
  // These two actions have a lot of logic attached to them such as mocks, so you need to delegate
  // these to specific logic per each specific load balancer. This makes sense, as the load
  // balancing algorithms sometimes use other components which are tightly coupled into the
  // algorithm logically.
  virtual void prefetch() PURE;
  virtual void chooseHost() PURE;
  virtual ~LoadBalancerFuzzBase() = default;

private:
  void replay(test::common::upstream::LoadBalancerTestCase input);
  // TODO: Move load balancer here?
};

class RandomLoadBalancerFuzzTest : public LoadBalancerFuzzBase {
public:
  RandomLoadBalancerFuzzTest();
  void initialize(test::common::upstream::LoadBalancerTestCase input) override;
  // Has mock logic for random
  void prefetch() override;
  void chooseHost() override;
  ~RandomLoadBalancerFuzzTest() = default;

  std::unique_ptr<RandomLoadBalancer> load_balancer_;
};

// TODO: Since each load balancer has a specific config, this will need to be addressed in their
// specific base classes. We can do this with an extended proto, which also will contain specific
// logic for mocks

} // namespace Upstream
} // namespace Envoy
