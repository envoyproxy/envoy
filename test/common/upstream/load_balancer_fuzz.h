




namespace Envoy {
namespace Upstream {


//TODO: Perhaps switch this class to a base class
class LoadBalancerFuzzBase {
public:
    //Intialize lb here?
    LoadBalancerFuzzBase() = default;
    virtual void initialize();
    void initializeAndReplay(test::common::upstream::LoadBalancerTestCase input);
    void addHostSet(uint64_t number_of_hosts_in_host_set);
    void updateHealthFlags(Hostset host_set, ); //Hostset or int, or mod the int with number of host sets
    void prefetch();
    void chooseHost();

private:
    void replay(test::common::upstream::LoadBalancerTestCase input);
    LoadBalancer load_balancer_; //Will be overriden with specific implementations
}

class RandomLoadBalancerFuzzTest : public LoadBalancerFuzzBase {
    void initialize() override;
}






} //namespace Upstream
} //namespace Envoy
