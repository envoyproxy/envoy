
#include "common/upstream/load_balancer_impl.h"

#include "test/common/upstream/load_balancer_fuzz.pb.validate.h"
#include "test/common/upstream/load_balancer_impl_test_utils.h"


namespace Envoy {
namespace Upstream {


//TODO: Perhaps switch this class to a base class
class LoadBalancerFuzzBase : public LoadBalancerFuzzTestBase {
public:
    //Intialize lb here?
    LoadBalancerFuzzBase() = default;
    virtual void initialize(test::common::upstream::LoadBalancerTestCase input);
    void initializeAndReplay(test::common::upstream::LoadBalancerTestCase input);
    //void addHostSet(uint64_t number_of_hosts_in_host_set);
    void updateHealthFlagsForAHostSet(bool failover_host_set, uint32_t num_hosts, uint32_t num_healthy_hosts, uint32_t num_degraded_hosts = 0, uint32_t num_excluded_hosts = 0); //Hostset or int, or mod the int with number of host sets
    //These two actions have a lot of logic attached to them such as mocks, so you need to delegate these to specific logic per each specific load balancer.
    //This makes sense, as the load balancing algorithms sometimes use other components which are coupled into the algorithm logically.
    virtual void prefetch();
    virtual void chooseHost();

private:
    void replay(test::common::upstream::LoadBalancerTestCase input);
    //LoadBalancer load_balancer_; //Will be overriden with specific implementations
};

class RandomLoadBalancerFuzzTest : public LoadBalancerFuzzBase {
    void initialize(test::common::upstream::LoadBalancerTestCase input) override;
    //Has interesting mock logic
    void prefetch() override;
    void chooseHost() override;
    RandomLoadBalancer load_balancer_;
};






} //namespace Upstream
} //namespace Envoy
