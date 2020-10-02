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
namespace {
/*
class LoadBalancerTestBase : public testing::TestWithParam<bool> {
protected:
  // Run all tests against both priority 0 and priority 1 host sets, to ensure
  // all the load balancers have equivalent functionality for failover host sets.
  MockHostSet& hostSet() { return GetParam() ? host_set_ : failover_host_set_; }

  LoadBalancerTestBase() : stats_(ClusterInfoImpl::generateStats(stats_store_)) {
    least_request_lb_config_.mutable_choice_count()->set_value(2);
  }

  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<MockPrioritySet> priority_set_;
  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  MockHostSet& failover_host_set_ = *priority_set_.getMockHostSet(1);
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  envoy::config::cluster::v3::Cluster::LeastRequestLbConfig least_request_lb_config_;
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

class LoadBalancerBaseTest : public LoadBalancerTestBase {
public:
  void updateHostSet(MockHostSet& host_set, uint32_t num_hosts, uint32_t num_healthy_hosts,
                     uint32_t num_degraded_hosts = 0, uint32_t num_excluded_hosts = 0) {
    ASSERT(num_healthy_hosts + num_degraded_hosts + num_excluded_hosts <= num_hosts);

    host_set.hosts_.clear();
    host_set.healthy_hosts_.clear();
    host_set.degraded_hosts_.clear();
    host_set.excluded_hosts_.clear();
    for (uint32_t i = 0; i < num_hosts; ++i) {
      host_set.hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:80"));
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
    host_set.runCallbacks({}, {});
  }

  template <typename T, typename FUNC>
  std::vector<T> aggregatePrioritySetsValues(TestLb& lb, FUNC func) {
    std::vector<T> ret;

    for (size_t i = 0; i < priority_set_.host_sets_.size(); ++i) {
      ret.push_back((lb.*func)(i));
    }

    return ret;
  }

  std::vector<uint32_t> getLoadPercentage() {
    return aggregatePrioritySetsValues<uint32_t>(lb_, &TestLb::percentageLoad);
  }

  std::vector<uint32_t> getDegradedLoadPercentage() {
    return aggregatePrioritySetsValues<uint32_t>(lb_, &TestLb::percentageDegradedLoad);
  }

  std::vector<bool> getPanic() {
    return aggregatePrioritySetsValues<bool>(lb_, &TestLb::isInPanic);
  }

  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  TestLb lb_{priority_set_, stats_, runtime_, random_, common_config_};
};
*/

} // namespace
} // namespace Upstream
} // namespace Envoy
