







namespace Envoy {
namespace Upstream {
namespace {

class LoadBalancerFuzzTestBase {
protected:
  // Run all tests against both priority 0 and priority 1 host sets, to ensure
  // all the load balancers have equivalent functionality for failover host sets.
  //MockHostSet& hostSet() { return GetParam() ? host_set_ : failover_host_set_; }

  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<MockPrioritySet> priority_set_;
  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  MockHostSet& failover_host_set_ = *priority_set_.getMockHostSet(1);
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
};

class TestLb : public LoadBalancerBase {
public:
  TestLb(const PrioritySet& priority_set, ClusterStats& stats, Runtime::Loader& runtime,
         Random::RandomGenerator& random,
         const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
      : LoadBalancerBase(priority_set, stats, runtime, random, common_config) {}
  using LoadBalancerBase::chooseHostSet;
  using LoadBalancerBase::isInPanic;
  using LoadBalancerBase::percentageDegradedLoad;
  using LoadBalancerBase::percentageLoad;

  HostConstSharedPtr chooseHostOnce(LoadBalancerContext*) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  HostConstSharedPtr peekAnotherHost(LoadBalancerContext*) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
};


} // namespace
} // namespace Upstream
} // namespace Envoy
