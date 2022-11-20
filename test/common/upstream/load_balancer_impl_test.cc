#include <bitset>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/network/utility.h"
#include "source/common/upstream/load_balancer_impl.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/logging.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ElementsAre;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {

class EdfLoadBalancerBasePeer {
public:
  static const std::chrono::milliseconds& slowStartWindow(EdfLoadBalancerBase& edf_lb) {
    return edf_lb.slow_start_window_;
  }
  static double aggression(EdfLoadBalancerBase& edf_lb) { return edf_lb.aggression_; }
  static const std::chrono::milliseconds latestHostAddedTime(EdfLoadBalancerBase& edf_lb) {
    return std::chrono::time_point_cast<std::chrono::milliseconds>(edf_lb.latest_host_added_time_)
        .time_since_epoch();
  }
  static double slowStartMinWeightPercent(const EdfLoadBalancerBase& edf_lb) {
    return edf_lb.slow_start_min_weight_percent_;
  }
};

class TestZoneAwareLoadBalancer : public ZoneAwareLoadBalancerBase {
public:
  TestZoneAwareLoadBalancer(
      const PrioritySet& priority_set, ClusterLbStats& lb_stats, Runtime::Loader& runtime,
      Random::RandomGenerator& random,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
      : ZoneAwareLoadBalancerBase(priority_set, nullptr, lb_stats, runtime, random, common_config) {
  }
  void runInvalidLocalitySourceType() {
    localitySourceType(static_cast<LoadBalancerBase::HostAvailability>(123));
  }
  void runInvalidSourceType() { sourceType(static_cast<LoadBalancerBase::HostAvailability>(123)); }
  HostConstSharedPtr chooseHostOnce(LoadBalancerContext*) override { PANIC("not implemented"); }
  HostConstSharedPtr peekAnotherHost(LoadBalancerContext*) override { PANIC("not implemented"); }
};

namespace {

class LoadBalancerTestBase : public Event::TestUsingSimulatedTime,
                             public testing::TestWithParam<bool> {
protected:
  // Run all tests against both priority 0 and priority 1 host sets, to ensure
  // all the load balancers have equivalent functionality for failover host sets.
  MockHostSet& hostSet() { return GetParam() ? host_set_ : failover_host_set_; }

  LoadBalancerTestBase()
      : stat_names_(stats_store_.symbolTable()), stats_(stat_names_, stats_store_) {
    least_request_lb_config_.mutable_choice_count()->set_value(2);
  }

  Stats::IsolatedStoreImpl stats_store_;
  ClusterLbStatNames stat_names_;
  ClusterLbStats stats_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<MockPrioritySet> priority_set_;
  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  MockHostSet& failover_host_set_ = *priority_set_.getMockHostSet(1);
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  envoy::config::cluster::v3::Cluster::LeastRequestLbConfig least_request_lb_config_;
  envoy::config::cluster::v3::Cluster::RoundRobinLbConfig round_robin_lb_config_;
};

class TestLb : public LoadBalancerBase {
public:
  TestLb(const PrioritySet& priority_set, ClusterLbStats& lb_stats, Runtime::Loader& runtime,
         Random::RandomGenerator& random,
         const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
      : LoadBalancerBase(priority_set, lb_stats, runtime, random, common_config) {}
  using LoadBalancerBase::chooseHostSet;
  using LoadBalancerBase::isInPanic;
  using LoadBalancerBase::percentageDegradedLoad;
  using LoadBalancerBase::percentageLoad;

  HostConstSharedPtr chooseHost(LoadBalancerContext*) override { PANIC("not implemented"); }

  HostConstSharedPtr peekAnotherHost(LoadBalancerContext*) override { PANIC("not implemented"); }
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
      host_set.hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:80", simTime()));
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

INSTANTIATE_TEST_SUITE_P(PrimaryOrFailover, LoadBalancerBaseTest, ::testing::Values(true));

// Basic test of host set selection.
TEST_P(LoadBalancerBaseTest, PrioritySelection) {
  NiceMock<Upstream::MockLoadBalancerContext> context;
  updateHostSet(host_set_, 1 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 1, 0);

  HealthyAndDegradedLoad priority_load{Upstream::HealthyLoad({100, 0, 0}),
                                       Upstream::DegradedLoad({0, 0, 0})};
  EXPECT_CALL(context, determinePriorityLoad(_, _, _)).WillRepeatedly(ReturnRef(priority_load));
  // Primary and failover are in panic mode. Load distribution is based
  // on the number of hosts regardless of their health.
  EXPECT_EQ(50, lb_.percentageLoad(0));
  EXPECT_EQ(50, lb_.percentageLoad(1));
  EXPECT_EQ(&host_set_, &lb_.chooseHostSet(&context, 0).first);

  // Modify number of hosts in failover, but leave them in the unhealthy state
  // primary and secondary are in panic mode, so load distribution is
  // based on number of host regardless of their health.
  updateHostSet(failover_host_set_, 2, 0);
  EXPECT_EQ(34, lb_.percentageLoad(0));
  EXPECT_EQ(66, lb_.percentageLoad(1));
  EXPECT_EQ(&host_set_, &lb_.chooseHostSet(&context, 0).first);

  // Update the priority set with a new priority level P=2 and ensure the host
  // is chosen
  MockHostSet& tertiary_host_set_ = *priority_set_.getMockHostSet(2);
  updateHostSet(tertiary_host_set_, 1 /* num_hosts */, 1 /* num_healthy_hosts */);
  EXPECT_EQ(0, lb_.percentageLoad(0));
  EXPECT_EQ(0, lb_.percentageLoad(1));
  EXPECT_EQ(100, lb_.percentageLoad(2));
  priority_load.healthy_priority_load_ = HealthyLoad({0u, 0u, 100});
  EXPECT_EQ(&tertiary_host_set_, &lb_.chooseHostSet(&context, 0).first);

  // Now add a healthy host in P=0 and make sure it is immediately selected.
  updateHostSet(host_set_, 1 /* num_hosts */, 1 /* num_healthy_hosts */);
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(100, lb_.percentageLoad(0));
  EXPECT_EQ(0, lb_.percentageLoad(2));
  priority_load.healthy_priority_load_ = HealthyLoad({100u, 0u, 0u});
  EXPECT_EQ(&host_set_, &lb_.chooseHostSet(&context, 0).first);

  // Remove the healthy host and ensure we fail back over to tertiary_host_set_
  updateHostSet(host_set_, 1 /* num_hosts */, 0 /* num_healthy_hosts */);
  EXPECT_EQ(0, lb_.percentageLoad(0));
  EXPECT_EQ(100, lb_.percentageLoad(2));
  priority_load.healthy_priority_load_ = HealthyLoad({0u, 0u, 100});
  EXPECT_EQ(&tertiary_host_set_, &lb_.chooseHostSet(&context, 0).first);
}

// Tests host selection with a randomized number of healthy, degraded and unhealthy hosts.
TEST_P(LoadBalancerBaseTest, PrioritySelectionFuzz) {
  TestRandomGenerator rand;

  // Determine total number of hosts.
  const auto total_hosts = 1 + (rand.random() % 10);

  NiceMock<Upstream::MockLoadBalancerContext> context;

  const auto host_set_hosts = rand.random() % total_hosts;

  if (host_set_hosts == 0) {
    updateHostSet(host_set_, 0, 0);
  } else {
    // We get on average 50% healthy hosts, 25% degraded hosts and 25% unhealthy hosts.
    const auto healthy_hosts = rand.random() % host_set_hosts;
    const auto degraded_hosts = rand.random() % (host_set_hosts - healthy_hosts);
    const auto unhealthy_hosts = host_set_hosts - healthy_hosts - degraded_hosts;

    updateHostSet(host_set_, host_set_hosts, unhealthy_hosts, degraded_hosts);
  }

  const auto failover_set_hosts = total_hosts - host_set_hosts;

  if (host_set_hosts == 0) {
    updateHostSet(failover_host_set_, 0, 0);
  } else {
    // We get on average 50% healthy hosts, 25% degraded hosts and 25% unhealthy hosts.
    const auto healthy_hosts = rand.random() % failover_set_hosts;
    const auto degraded_hosts = rand.random() % (failover_set_hosts - healthy_hosts);
    const auto unhealthy_hosts = failover_set_hosts - healthy_hosts - degraded_hosts;

    updateHostSet(failover_host_set_, failover_set_hosts, unhealthy_hosts, degraded_hosts);
  }

  EXPECT_CALL(context, determinePriorityLoad(_, _, _))
      .WillRepeatedly(
          Invoke([](const auto&, const auto& original_load,
                    const auto&) -> const HealthyAndDegradedLoad& { return original_load; }));

  for (uint64_t i = 0; i < total_hosts; ++i) {
    const auto hs = lb_.chooseHostSet(&context, 0);
    switch (hs.second) {
    case LoadBalancerBase::HostAvailability::Healthy:
      // Either we selected one of the healthy hosts or we failed to select anything and
      // defaulted to healthy.
      EXPECT_TRUE(!hs.first.healthyHosts().empty() ||
                  (hs.first.healthyHosts().empty() && hs.first.degradedHosts().empty()));
      break;
    case LoadBalancerBase::HostAvailability::Degraded:
      EXPECT_FALSE(hs.first.degradedHosts().empty());
      break;
    }
  }
}

// Test of host set selection with priority filter
TEST_P(LoadBalancerBaseTest, PrioritySelectionWithFilter) {
  NiceMock<Upstream::MockLoadBalancerContext> context;

  HealthyAndDegradedLoad priority_load{Upstream::HealthyLoad({0u, 100u}),
                                       Upstream::DegradedLoad({0, 0})};
  // return a filter that excludes priority 0
  EXPECT_CALL(context, determinePriorityLoad(_, _, _)).WillRepeatedly(ReturnRef(priority_load));

  updateHostSet(host_set_, 1 /* num_hosts */, 1 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 1, 1);

  // Since we've excluded P0, we should pick the failover host set
  EXPECT_EQ(failover_host_set_.priority(), lb_.chooseHostSet(&context, 0).first.priority());

  updateHostSet(host_set_, 1 /* num_hosts */, 0 /* num_healthy_hosts */,
                1 /* num_degraded_hosts */);
  updateHostSet(failover_host_set_, 1, 0, 1);

  // exclude priority 0 for degraded hosts
  priority_load.healthy_priority_load_ = Upstream::HealthyLoad({0, 0});
  priority_load.degraded_priority_load_ = Upstream::DegradedLoad({0, 100});

  // Since we've excluded P0, we should pick the failover host set
  EXPECT_EQ(failover_host_set_.priority(), lb_.chooseHostSet(&context, 0).first.priority());
}

TEST_P(LoadBalancerBaseTest, OverProvisioningFactor) {
  // Default overprovisioning factor 1.4 makes P0 receives 70% load.
  updateHostSet(host_set_, 4, 2);
  updateHostSet(failover_host_set_, 4, 2);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(70, 30));

  // Set overprovisioning factor to 1, now it should be proportioned to healthy ratio.
  host_set_.setOverprovisioningFactor(100);
  updateHostSet(host_set_, 4, 2);
  failover_host_set_.setOverprovisioningFactor(100);
  updateHostSet(failover_host_set_, 4, 2);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(50, 50));
}

TEST_P(LoadBalancerBaseTest, GentleFailover) {
  // With 100% of P=0 hosts healthy, P=0 gets all the load.
  // None of the levels is in Panic mode
  updateHostSet(host_set_, 1, 1);
  updateHostSet(failover_host_set_, 1, 1);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(100, 0));
  ASSERT_THAT(getPanic(), ElementsAre(false, false));

  // Health P=0 == 50*1.4 == 70
  // Total health = 70 + 70 >= 100%. None of the levels should be in panic mode.
  updateHostSet(host_set_, 2 /* num_hosts */, 1 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 2 /* num_hosts */, 1 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(70, 30));
  ASSERT_THAT(getPanic(), ElementsAre(false, false));

  // Health P=0 == 25*1.4 == 35   P=1 is healthy so takes all spillover.
  // Total health = 35+100 >= 100%. P=0 is below Panic level but it is ignored, because
  // Total health >= 100%.
  updateHostSet(host_set_, 4 /* num_hosts */, 1 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 2 /* num_hosts */, 2 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(35, 65));
  ASSERT_THAT(getPanic(), ElementsAre(false, false));

  // Health P=0 == 25*1.4 == 35   P=1 == 35
  // Health is then scaled up by (100 / (35 + 35) == 50)
  // Total health = 35% + 35% is less than 100%. Panic levels per priority kick in.
  updateHostSet(host_set_, 4 /* num_hosts */, 1 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 4 /* num_hosts */, 1 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(50, 50));
  ASSERT_THAT(getPanic(), ElementsAre(true, true));

  // Health P=0 == 100*1.4 == 35 P=1 == 35
  // Since 3 hosts are excluded, P=0 should be considered fully healthy.
  // Total health = 100% + 35% is greater than 100%. Panic should not trigger.
  updateHostSet(host_set_, 4 /* num_hosts */, 1 /* num_healthy_hosts */, 0 /* num_degraded_hosts
                                                                            */
                ,
                3 /* num_excluded_hosts */);
  updateHostSet(failover_host_set_, 5 /* num_hosts */, 1 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(100, 0));
  ASSERT_THAT(getPanic(), ElementsAre(false, false));

  // Health P=0 == 100*1.4 == 35 P=1 == 35
  // Total health = 35% is less than 100%.
  // All priorities are in panic mode (situation called TotalPanic)
  // Load is distributed based on number of hosts regardless of their health status.
  // P=0 and P=1 have 4 hosts each so each priority will receive 50% of the traffic.
  updateHostSet(host_set_, 4 /* num_hosts */, 0 /* num_healthy_hosts */, 0 /* num_degraded_hosts
                                                                            */
                ,
                4 /* num_excluded_hosts */);
  updateHostSet(failover_host_set_, 4 /* num_hosts */, 1 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(50, 50));
  ASSERT_THAT(getPanic(), ElementsAre(true, true));

  // Make sure that in TotalPanic mode (all levels are in Panic),
  // load distribution depends only on number of hosts.
  // excluded_hosts should not be taken into account.
  // P=0 has 4 hosts with 1 excluded, P=1 has 6 hosts with 2 excluded.
  // P=0 should receive 4/(4+6)=40% of traffic
  // P=1 should receive 6/(4+6)=60% of traffic
  updateHostSet(host_set_, 4 /* num_hosts */, 0 /* num_healthy_hosts */, 0 /* num_degraded_hosts
                                                                            */
                ,
                1 /* num_excluded_hosts */);
  updateHostSet(failover_host_set_, 6 /* num_hosts */, 1 /* num_healthy_hosts */,
                0 /* num_degraded_hosts */, 2 /* num_excluded_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(40, 60));
  ASSERT_THAT(getPanic(), ElementsAre(true, true));
}

TEST_P(LoadBalancerBaseTest, GentleFailoverWithExtraLevels) {
  // Add a third host set. Again with P=0 healthy, all traffic goes there.
  MockHostSet& tertiary_host_set_ = *priority_set_.getMockHostSet(2);
  updateHostSet(host_set_, 1, 1);
  updateHostSet(failover_host_set_, 1, 1);
  updateHostSet(tertiary_host_set_, 1, 1);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(100, 0, 0));
  ASSERT_THAT(getPanic(), ElementsAre(false, false, false));

  // Health P=0 == 50*1.4 == 70
  // Health P=0 == 50, so can take the 30% spillover.
  updateHostSet(host_set_, 2 /* num_hosts */, 1 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 2 /* num_hosts */, 1 /* num_healthy_hosts */);
  updateHostSet(tertiary_host_set_, 2 /* num_hosts */, 1 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(70, 30, 0));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(0, 0, 0));

  // Health P=0 == 25*1.4 == 35   P=1 is healthy so takes all spillover.
  updateHostSet(host_set_, 4 /* num_hosts */, 1 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 2 /* num_hosts */, 2 /* num_healthy_hosts */);
  updateHostSet(tertiary_host_set_, 2 /* num_hosts */, 1 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(35, 65, 0));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(0, 0, 0));

  // This is the first test where health (P=0 + P=1 < 100)
  // Health P=0 == 25*1.4 == 35   P=1 == 35  P=2 == 35
  updateHostSet(host_set_, 4 /* num_hosts */, 1 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 4 /* num_hosts */, 1 /* num_healthy_hosts */);
  updateHostSet(tertiary_host_set_, 4 /* num_hosts */, 1 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(35, 35, 30));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(0, 0, 0));

  // This is the first test where (health P=0 + P=1 < 100)
  // Health P=0 == 25*1.4 == 35   P=1 == 35  P=2 == 35
  updateHostSet(host_set_, 4 /* num_hosts */, 1 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 4 /* num_hosts */, 1 /* num_healthy_hosts */);
  updateHostSet(tertiary_host_set_, 4 /* num_hosts */, 1 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(35, 35, 30));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(0, 0, 0));

  // Now all health is (20% * 1.5 == 28). 28 * 3 < 100 so we have to scale.
  // Each Priority level gets 33% of the load, with P=0 picking up the rounding error.
  updateHostSet(host_set_, 5 /* num_hosts */, 1 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 5 /* num_hosts */, 1 /* num_healthy_hosts */);
  updateHostSet(tertiary_host_set_, 5 /* num_hosts */, 1 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(34, 33, 33));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(0, 0, 0));
  ASSERT_THAT(getPanic(), ElementsAre(true, true, true));

  // Levels P=0 and P=1 are totally down. P=2 is totally healthy.
  // 100% of the traffic should go to P=2 and P=0 and P=1 should
  // not be in panic mode.
  updateHostSet(host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(tertiary_host_set_, 5 /* num_hosts */, 5 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(0, 0, 100));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(0, 0, 0));
  ASSERT_THAT(getPanic(), ElementsAre(false, false, false));

  // Levels P=0 and P=1 are totally down. P=2 is 80*1.4 >= 100% healthy.
  // 100% of the traffic should go to P=2 and P=0 and P=1 should
  // not be in panic mode.
  updateHostSet(host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(tertiary_host_set_, 5 /* num_hosts */, 4 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(0, 0, 100));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(0, 0, 0));
  ASSERT_THAT(getPanic(), ElementsAre(false, false, false));

  // Levels P=0 and P=1 are totally down. P=2 is 40*1.4=56%% healthy.
  // 100% of the traffic should go to P=2. All levels P=0, P=1 and P=2 should
  // be in panic mode.
  // Since all levels are in panic mode load distribution is based
  // on number of hosts in each level.
  updateHostSet(host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(tertiary_host_set_, 5 /* num_hosts */, 2 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(34, 33, 33));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(0, 0, 0));
  ASSERT_THAT(getPanic(), ElementsAre(true, true, true));

  // Level P=0 is totally degraded. P=1 is 40*1.4=56% healthy and 40*1.4=56% degraded. P=2 is
  // 40*1.4=56%% healthy. 100% of the traffic should go to P=2. No priorities should be in panic
  // mode.
  updateHostSet(host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */,
                5 /* num_degraded_hosts */);
  updateHostSet(failover_host_set_, 5 /* num_hosts */, 2 /* num_healthy_hosts */,
                2 /* num_degraded_hosts */);
  updateHostSet(tertiary_host_set_, 5 /* num_hosts */, 2 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(0, 56, 44));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(0, 0, 0));
  ASSERT_THAT(getPanic(), ElementsAre(false, false, false));

  // All levels are completely down - situation called TotalPanic.
  // Load is distributed based on the number
  // of hosts in the priority in relation to the total number of hosts.
  // Here the total number of hosts is 10.
  // priority 0 will receive 5/10: 50% of the traffic
  // priority 1 will receive 3/10: 30% of the traffic
  // priority 2 will receive 2/10: 20% of the traffic
  updateHostSet(host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 3 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(tertiary_host_set_, 2 /* num_hosts */, 0 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(50, 30, 20));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(0, 0, 0));
  ASSERT_THAT(getPanic(), ElementsAre(true, true, true));

  // Rounding errors should be picked up by the first priority.
  // All priorities are in panic mode - situation called TotalPanic.
  // Load is distributed based on the number
  // of hosts in the priority in relation to the total number of hosts.
  // Total number of hosts is 5+6+3=14.
  // priority 0 should receive 5/14=37% of traffic
  // priority 1 should receive 6/14=42% of traffic
  // priority 2 should receive 3/14=21% of traffic
  updateHostSet(host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 6 /* num_hosts */, 2 /* num_healthy_hosts */);
  updateHostSet(tertiary_host_set_, 3 /* num_hosts */, 1 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(37, 42, 21));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(0, 0, 0));
  ASSERT_THAT(getPanic(), ElementsAre(true, true, true));

  // Load should spill over into degraded.
  updateHostSet(host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */,
                1 /* num_degraded_hosts */);
  updateHostSet(failover_host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */,
                5 /* num_degraded_hosts */);
  updateHostSet(tertiary_host_set_, 5 /* num_hosts */, 1 /* num_healthy_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(0, 0, 28));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(28, 44, 0));

  // Rounding errors should be picked up by the first priority with degraded hosts when
  // there are no healthy priorities.
  // Disable panic threshold to prevent total panic from kicking in.
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(0));
  updateHostSet(host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */,
                2 /* num_degraded_hosts */);
  updateHostSet(tertiary_host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */,
                1 /* num_degraded_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(0, 0, 0));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(0, 67, 33));

  // Simulate Total Panic mode. There is no healthy hosts, but there are
  // degraded hosts. Because there is Total Panic, load is distributed
  // based just on number of hosts in priorities regardless of its health.
  // Rounding errors should be picked up by the first priority.
  // Enable back panic threshold.
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  updateHostSet(host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */,
                2 /* num_degraded_hosts */);
  updateHostSet(tertiary_host_set_, 5 /* num_hosts */, 0 /* num_healthy_hosts */,
                1 /* num_degraded_hosts */);
  ASSERT_THAT(getLoadPercentage(), ElementsAre(34, 33, 33));
  ASSERT_THAT(getDegradedLoadPercentage(), ElementsAre(0, 0, 0));

  // Rounding error should be allocated to the first non-empty priority
  // In this test P=0 is not empty.
  updateHostSet(host_set_, 3 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 3 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(tertiary_host_set_, 3 /* num_hosts */, 0 /* num_healthy_hosts */);
  ASSERT_THAT(getPanic(), ElementsAre(true, true, true));
  ASSERT_THAT(getLoadPercentage(), ElementsAre(34, 33, 33));

  // Rounding error should be allocated to the first non-empty priority
  // In this test P=0 is empty and P=1 is not empty.
  updateHostSet(host_set_, 0 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 6 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(tertiary_host_set_, 3 /* num_hosts */, 0 /* num_healthy_hosts */);
  ASSERT_THAT(getPanic(), ElementsAre(true, true, true));
  ASSERT_THAT(getLoadPercentage(), ElementsAre(0, 67, 33));
  // In this test P=1 is not empty.
  updateHostSet(host_set_, 3 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(failover_host_set_, 3 /* num_hosts */, 0 /* num_healthy_hosts */);
  updateHostSet(tertiary_host_set_, 3 /* num_hosts */, 0 /* num_healthy_hosts */);
  ASSERT_THAT(getPanic(), ElementsAre(true, true, true));
  ASSERT_THAT(getLoadPercentage(), ElementsAre(34, 33, 33));
}

TEST_P(LoadBalancerBaseTest, BoundaryConditions) {
  TestRandomGenerator rand;
  uint32_t num_priorities = rand.random() % 10;

  for (uint32_t i = 0; i < num_priorities; ++i) {
    uint32_t num_hosts = rand.random() % 100;
    uint32_t healthy_hosts = std::min<uint32_t>(num_hosts, rand.random() % 100);
    // Make sure random health situations don't trigger the assert in recalculatePerPriorityState
    updateHostSet(*priority_set_.getMockHostSet(i), num_hosts, healthy_hosts);
  }
}

class TestZoneAwareLb : public ZoneAwareLoadBalancerBase {
public:
  TestZoneAwareLb(const PrioritySet& priority_set, ClusterLbStats& lb_stats,
                  Runtime::Loader& runtime, Random::RandomGenerator& random,
                  const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config)
      : ZoneAwareLoadBalancerBase(priority_set, nullptr, lb_stats, runtime, random, common_config) {
  }

  HostConstSharedPtr chooseHostOnce(LoadBalancerContext*) override {
    return choose_host_once_host_;
  }
  HostConstSharedPtr peekAnotherHost(LoadBalancerContext*) override { PANIC("not implemented"); }

  HostConstSharedPtr choose_host_once_host_{std::make_shared<NiceMock<MockHost>>()};
};

// Used to test common functions of ZoneAwareLoadBalancerBase.
class ZoneAwareLoadBalancerBaseTest : public LoadBalancerTestBase {
public:
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  TestZoneAwareLb lb_{priority_set_, stats_, runtime_, random_, common_config_};
  TestZoneAwareLoadBalancer lbx_{priority_set_, stats_, runtime_, random_, common_config_};
};

// Tests the source type static methods in zone aware load balancer.
TEST_F(ZoneAwareLoadBalancerBaseTest, SourceTypeMethods) {
  { EXPECT_ENVOY_BUG(lbx_.runInvalidLocalitySourceType(), "unexpected locality source type enum"); }

  { EXPECT_ENVOY_BUG(lbx_.runInvalidSourceType(), "unexpected source type enum"); }
}

TEST_F(ZoneAwareLoadBalancerBaseTest, BaseMethods) {
  EXPECT_FALSE(lb_.lifetimeCallbacks().has_value());
  std::vector<uint8_t> hash_key;
  auto mock_host = std::make_shared<NiceMock<MockHost>>();
  EXPECT_FALSE(lb_.selectExistingConnection(nullptr, *mock_host, hash_key).has_value());
}

class RoundRobinLoadBalancerTest : public LoadBalancerTestBase {
public:
  void init(bool need_local_cluster, bool locality_weight_aware = false) {
    if (need_local_cluster) {
      local_priority_set_ = std::make_shared<PrioritySetImpl>();
      local_priority_set_->getOrCreateHostSet(0);
    }

    if (locality_weight_aware) {
      common_config_.mutable_locality_weighted_lb_config();
    }

    lb_ = std::make_shared<RoundRobinLoadBalancer>(priority_set_, local_priority_set_.get(), stats_,
                                                   runtime_, random_, common_config_,
                                                   round_robin_lb_config_, simTime());
  }

  // Updates priority 0 with the given hosts and hosts_per_locality.
  void updateHosts(HostVectorConstSharedPtr hosts,
                   HostsPerLocalityConstSharedPtr hosts_per_locality) {
    local_priority_set_->updateHosts(
        0,
        updateHostsParams(hosts, hosts_per_locality,
                          std::make_shared<const HealthyHostVector>(*hosts), hosts_per_locality),
        {}, empty_host_vector_, empty_host_vector_, absl::nullopt);
  }

  void peekThenPick(std::vector<int> picks) {
    for (auto i : picks) {
      EXPECT_EQ(hostSet().healthy_hosts_[i], lb_->peekAnotherHost(nullptr));
    }
    for (auto i : picks) {
      EXPECT_EQ(hostSet().healthy_hosts_[i], lb_->chooseHost(nullptr));
    }
  }

  std::shared_ptr<PrioritySetImpl> local_priority_set_;
  std::shared_ptr<LoadBalancer> lb_;
  HostsPerLocalityConstSharedPtr empty_locality_;
  HostVector empty_host_vector_;
};

// For the tests which mutate primary and failover host sets explicitly, only
// run once.
using FailoverTest = RoundRobinLoadBalancerTest;

// Ensure if all the hosts with priority 0 unhealthy, the next priority hosts are used.
TEST_P(FailoverTest, BasicFailover) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  failover_host_set_.healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:82", simTime())};
  failover_host_set_.hosts_ = failover_host_set_.healthy_hosts_;
  init(false);
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb_->peekAnotherHost(nullptr));
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb_->chooseHost(nullptr));
}

// Ensure if all the hosts with priority 0 degraded, the first priority degraded hosts are used.
TEST_P(FailoverTest, BasicDegradedHosts) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  host_set_.degraded_hosts_ = host_set_.hosts_;
  failover_host_set_.hosts_ = failover_host_set_.healthy_hosts_;
  init(false);
  // We don't preconnect degraded hosts.
  EXPECT_EQ(nullptr, lb_->peekAnotherHost(nullptr));
  EXPECT_EQ(host_set_.degraded_hosts_[0], lb_->chooseHost(nullptr));
}

// Ensure if all the hosts with priority 0 degraded, but healthy hosts in the failover, the healthy
// hosts in the second priority are used.
TEST_P(FailoverTest, BasicFailoverDegradedHosts) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  host_set_.degraded_hosts_ = host_set_.hosts_;
  failover_host_set_.healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:82", simTime())};
  failover_host_set_.hosts_ = failover_host_set_.healthy_hosts_;
  init(false);
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb_->chooseHost(nullptr));
}

// Test that extending the priority set with an existing LB causes the correct updates.
TEST_P(FailoverTest, PriorityUpdatesWithLocalHostSet) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  failover_host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  init(false);
  // With both the primary and failover hosts unhealthy, we should select an
  // unhealthy primary host.
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Update the priority set with a new priority level P=2 and ensure the host
  // is chosen
  MockHostSet& tertiary_host_set_ = *priority_set_.getMockHostSet(2);
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}));
  tertiary_host_set_.hosts_ = *hosts;
  tertiary_host_set_.healthy_hosts_ = tertiary_host_set_.hosts_;
  HostVector add_hosts;
  add_hosts.push_back(tertiary_host_set_.hosts_[0]);
  tertiary_host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Now add a healthy host in P=0 and make sure it is immediately selected.
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Remove the healthy host and ensure we fail back over to tertiary_host_set_
  host_set_.healthy_hosts_ = {};
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));
}

// Test that extending the priority set with an existing LB causes the correct updates when the
// cluster is configured to disable on panic.
TEST_P(FailoverTest, PriorityUpdatesWithLocalHostSetDisableOnPanic) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  failover_host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  common_config_.mutable_zone_aware_lb_config()->set_fail_traffic_on_panic(true);

  init(false);
  // With both the primary and failover hosts unhealthy, we should select no host.
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));

  // Update the priority set with a new priority level P=2 and ensure the host
  // is chosen
  MockHostSet& tertiary_host_set_ = *priority_set_.getMockHostSet(2);
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}));
  tertiary_host_set_.hosts_ = *hosts;
  tertiary_host_set_.healthy_hosts_ = tertiary_host_set_.hosts_;
  HostVector add_hosts;
  add_hosts.push_back(tertiary_host_set_.hosts_[0]);
  tertiary_host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Now add a healthy host in P=0 and make sure it is immediately selected.
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Remove the healthy host and ensure we fail back over to tertiary_host_set_
  host_set_.healthy_hosts_ = {};
  host_set_.runCallbacks({}, {});
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));
}

// Test extending the priority set.
TEST_P(FailoverTest, ExtendPrioritiesUpdatingPrioritySet) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  failover_host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  init(true);
  // With both the primary and failover hosts unhealthy, we should select an
  // unhealthy primary host.
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Update the priority set with a new priority level P=2
  // As it has healthy hosts, it should be selected.
  MockHostSet& tertiary_host_set_ = *priority_set_.getMockHostSet(2);
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}));
  tertiary_host_set_.hosts_ = *hosts;
  tertiary_host_set_.healthy_hosts_ = tertiary_host_set_.hosts_;
  HostVector add_hosts;
  add_hosts.push_back(tertiary_host_set_.hosts_[0]);
  tertiary_host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Now add a healthy host in P=0 and make sure it is immediately selected.
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));
}

TEST_P(FailoverTest, ExtendPrioritiesWithLocalPrioritySet) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  failover_host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  init(true);
  // With both the primary and failover hosts unhealthy, we should select an
  // unhealthy primary host.
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Update the host set with a new priority level. We should start selecting
  // hosts from that level as it has viable hosts.
  MockHostSet& tertiary_host_set_ = *priority_set_.getMockHostSet(2);
  HostVectorSharedPtr hosts2(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:84", simTime())}));
  tertiary_host_set_.hosts_ = *hosts2;
  tertiary_host_set_.healthy_hosts_ = tertiary_host_set_.hosts_;
  HostVector add_hosts;
  add_hosts.push_back(tertiary_host_set_.hosts_[0]);
  tertiary_host_set_.runCallbacks(add_hosts, {});
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));

  // Update the local hosts. We're not doing locality based routing in this
  // test, but it should at least do no harm.
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}));
  updateHosts(hosts, HostsPerLocalityImpl::empty());
  EXPECT_EQ(tertiary_host_set_.hosts_[0], lb_->chooseHost(nullptr));
}

// Verifies that the number of warmed hosts is used to compute priority spillover.
TEST_P(FailoverTest, PrioritiesWithNotAllWarmedHosts) {
  // To begin with we set up the following:
  // P0: 1 healthy, 1 unhealthy, 1 warmed.
  // P1: 1 healthy.
  // We then expect no spillover, since P0 is still overprovisioned.
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  host_set_.healthy_hosts_ = {host_set_.hosts_[0]};
  failover_host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:82", simTime())};
  failover_host_set_.healthy_hosts_ = failover_host_set_.hosts_;
  init(true);

  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(host_set_.hosts_[0], lb_->chooseHost(nullptr));
}

// Verifies that we handle zero warmed hosts.
TEST_P(FailoverTest, PrioritiesWithZeroWarmedHosts) {
  // To begin with we set up the following:
  // P0: 2 unhealthy, 0 warmed.
  // P1: 1 healthy.
  // We then expect all the traffic to spill over to P1 since P0 has an effective load of zero.
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  failover_host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:82", simTime())};
  failover_host_set_.healthy_hosts_ = failover_host_set_.hosts_;

  init(true);

  EXPECT_EQ(failover_host_set_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(failover_host_set_.hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(failover_host_set_.hosts_[0], lb_->chooseHost(nullptr));
}

INSTANTIATE_TEST_SUITE_P(PrimaryOrFailover, FailoverTest, ::testing::Values(true));

TEST_P(RoundRobinLoadBalancerTest, NoHosts) {
  init(false);
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, SingleHost) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  init(false);
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, Normal) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  init(false);

  // Make sure the round robin pattern works for peeking.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->peekAnotherHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->peekAnotherHost(nullptr));

  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));

  // Make sure that if picks get ahead of peeks, peeks resume at the next pick.
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->peekAnotherHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));

  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));

  // Change host set with no peeks in progress
  hostSet().healthy_hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:82", simTime()));
  hostSet().hosts_.push_back(hostSet().healthy_hosts_.back());
  hostSet().runCallbacks({hostSet().healthy_hosts_.back()}, {});
  peekThenPick({2, 0, 1});
  peekThenPick({2});

  // Now peek a few extra to push the index forward, alter the host set, and
  // make sure the index is restored to 0.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->peekAnotherHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->peekAnotherHost(nullptr));

  hostSet().healthy_hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:83", simTime()));
  hostSet().hosts_.push_back(hostSet().healthy_hosts_.back());
  hostSet().runCallbacks({hostSet().healthy_hosts_.back()}, {hostSet().healthy_hosts_.front()});
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  peekThenPick({2, 3});
}

// Validate that the RNG seed influences pick order.
TEST_P(RoundRobinLoadBalancerTest, Seed) {
  hostSet().healthy_hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
      makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
  };
  hostSet().hosts_ = hostSet().healthy_hosts_;
  EXPECT_CALL(random_, random()).WillRepeatedly(Return(1));
  init(false);
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, Locality) {
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                                            makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                                            makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{(*hosts)[1]}, {(*hosts)[0]}, {(*hosts)[2]}});
  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = hosts_per_locality;
  init(false, true);
  // chooseHealthyLocality() return value determines which locality we use.
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(1));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(1));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  // When there is no locality, we RR over all available hosts.
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(absl::optional<uint32_t>()));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(absl::optional<uint32_t>()));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(absl::optional<uint32_t>()));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, DegradedLocality) {
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                                            makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                                            makeTestHost(info_, "tcp://127.0.0.1:84", simTime())}));
  HostVectorSharedPtr healthy_hosts(new HostVector({(*hosts)[0]}));
  HostVectorSharedPtr degraded_hosts(new HostVector({(*hosts)[1], (*hosts)[2]}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{(*hosts)[0]}, {(*hosts)[1], (*hosts)[2]}});
  HostsPerLocalitySharedPtr healthy_hosts_per_locality = makeHostsPerLocality({{(*hosts)[0]}, {}});
  HostsPerLocalitySharedPtr degraded_hosts_per_locality =
      makeHostsPerLocality({{}, {(*hosts)[1], (*hosts)[2]}});

  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_ = *healthy_hosts;
  hostSet().degraded_hosts_ = *degraded_hosts;
  hostSet().hosts_per_locality_ = hosts_per_locality;
  hostSet().healthy_hosts_per_locality_ = healthy_hosts_per_locality;
  hostSet().degraded_hosts_per_locality_ = degraded_hosts_per_locality;
  init(false, true);

  EXPECT_CALL(random_, random()).WillOnce(Return(50)).WillOnce(Return(0));
  // Since we're split between healthy and degraded, the LB should call into both
  // chooseHealthyLocality and chooseDegradedLocality.
  EXPECT_CALL(hostSet(), chooseDegradedLocality()).WillOnce(Return(1));
  EXPECT_EQ(hostSet().degraded_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_CALL(hostSet(), chooseHealthyLocality()).WillOnce(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, Weighted) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), 1),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), 2)};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  init(false);
  // Initial weights respected.
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  // Modify weights, we converge on new weighting after one pick cycle.
  hostSet().healthy_hosts_[0]->weight(2);
  hostSet().healthy_hosts_[1]->weight(1);
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  // Add a host, it should participate in next round of scheduling.
  hostSet().healthy_hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:82", simTime(), 3));
  hostSet().hosts_.push_back(hostSet().healthy_hosts_.back());
  hostSet().runCallbacks({hostSet().healthy_hosts_.back()}, {});
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  // Remove last two hosts, add a new one with different weights.
  HostVector removed_hosts = {hostSet().hosts_[1], hostSet().hosts_[2]};
  hostSet().healthy_hosts_.pop_back();
  hostSet().healthy_hosts_.pop_back();
  hostSet().hosts_.pop_back();
  hostSet().hosts_.pop_back();
  hostSet().healthy_hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:83", simTime(), 4));
  hostSet().hosts_.push_back(hostSet().healthy_hosts_.back());
  hostSet().healthy_hosts_[0]->weight(1);
  hostSet().runCallbacks({hostSet().healthy_hosts_.back()}, removed_hosts);
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

// Validate that the RNG seed influences pick order when weighted RR.
TEST_P(RoundRobinLoadBalancerTest, WeightedSeed) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), 1),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), 2)};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  EXPECT_CALL(random_, random()).WillRepeatedly(Return(1));
  init(false);
  // Initial weights respected.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, MaxUnhealthyPanic) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:83", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:84", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:85", simTime())};

  init(false);
  EXPECT_EQ(hostSet().hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().hosts_[2], lb_->chooseHost(nullptr));

  // Take the threshold back above the panic threshold.
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:83", simTime())};
  hostSet().runCallbacks({}, {});

  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));

  EXPECT_EQ(3UL, stats_.lb_healthy_panic_.value());
}

// Test that no hosts are selected when fail_traffic_on_panic is enabled.
TEST_P(RoundRobinLoadBalancerTest, MaxUnhealthyPanicDisableOnPanic) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:83", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:84", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:85", simTime())};

  common_config_.mutable_zone_aware_lb_config()->set_fail_traffic_on_panic(true);

  init(false);
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));

  // Take the threshold back above the panic threshold.
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:83", simTime())};
  hostSet().runCallbacks({}, {});

  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));

  EXPECT_EQ(1UL, stats_.lb_healthy_panic_.value());
}

// Ensure if the panic threshold is 0%, panic mode is disabled.
TEST_P(RoundRobinLoadBalancerTest, DisablePanicMode) {
  hostSet().healthy_hosts_ = {};
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};

  common_config_.mutable_healthy_panic_threshold()->set_value(0);

  init(false);
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(0));
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
  EXPECT_EQ(0UL, stats_.lb_healthy_panic_.value());
}

// Test of host set selection with host filter
TEST_P(RoundRobinLoadBalancerTest, HostSelectionWithFilter) {
  NiceMock<Upstream::MockLoadBalancerContext> context;

  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                                            makeTestHost(info_, "tcp://127.0.0.1:81", simTime())}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:81", simTime())}});

  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = hosts_per_locality;

  init(false);

  // return a predicate that only accepts the first host
  EXPECT_CALL(context, shouldSelectAnotherHost(_))
      .WillRepeatedly(Invoke([&](const Host& host) -> bool {
        return host.address()->asString() != hostSet().hosts_[0]->address()->asString();
      }));
  HealthyAndDegradedLoad priority_load{Upstream::HealthyLoad({0, 0}),
                                       Upstream::DegradedLoad({0, 0})};

  if (GetParam()) {
    priority_load.healthy_priority_load_ = HealthyLoad({100u, 0u});
  } else {
    priority_load.healthy_priority_load_ = HealthyLoad({0u, 100u});
  }
  EXPECT_CALL(context, determinePriorityLoad(_, _, _)).WillRepeatedly(ReturnRef(priority_load));
  EXPECT_CALL(context, hostSelectionRetryCount()).WillRepeatedly(Return(2));

  // Calling chooseHost multiple times always returns host one, since the filter will reject
  // the other host.
  EXPECT_EQ(hostSet().hosts_[0], lb_->chooseHost(&context));
  EXPECT_EQ(hostSet().hosts_[0], lb_->chooseHost(&context));
  EXPECT_EQ(hostSet().hosts_[0], lb_->chooseHost(&context));

  // By setting the retry counter to zero, we effectively disable the filter.
  EXPECT_CALL(context, hostSelectionRetryCount()).WillRepeatedly(Return(0));

  EXPECT_EQ(hostSet().hosts_[1], lb_->chooseHost(&context));
  EXPECT_EQ(hostSet().hosts_[0], lb_->chooseHost(&context));
  EXPECT_EQ(hostSet().hosts_[1], lb_->chooseHost(&context));
}

TEST_P(RoundRobinLoadBalancerTest, ZoneAwareSmallCluster) {
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                                            makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                                            makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:81", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}});

  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = hosts_per_locality;
  common_config_.mutable_healthy_panic_threshold()->set_value(0);
  common_config_.mutable_zone_aware_lb_config()->mutable_routing_enabled()->set_value(98);
  common_config_.mutable_zone_aware_lb_config()->mutable_min_cluster_size()->set_value(7);
  init(true);
  updateHosts(hosts, hosts_per_locality);

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 0))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 98))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 7))
      .WillRepeatedly(Return(7));

  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));

  if (&hostSet() == &host_set_) {
    // Cluster size is computed once at zone aware struct regeneration point.
    EXPECT_EQ(1U, stats_.lb_zone_cluster_too_small_.value());
  } else {
    EXPECT_EQ(0U, stats_.lb_zone_cluster_too_small_.value());
    return;
  }
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 7))
      .WillRepeatedly(Return(1));
  // Trigger reload.
  updateHosts(hosts, hosts_per_locality);
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, NoZoneAwareDifferentZoneSize) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                                            makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                                            makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}));
  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:81", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}});
  HostsPerLocalitySharedPtr local_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:81", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())}});

  hostSet().healthy_hosts_ = *hosts;
  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  common_config_.mutable_healthy_panic_threshold()->set_value(100);
  common_config_.mutable_zone_aware_lb_config()->mutable_routing_enabled()->set_value(98);
  common_config_.mutable_zone_aware_lb_config()->mutable_min_cluster_size()->set_value(7);
  init(true);
  updateHosts(hosts, local_hosts_per_locality);

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 100))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 98))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 7))
      .WillRepeatedly(Return(7));

  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_number_differs_.value());
}

TEST_P(RoundRobinLoadBalancerTest, ZoneAwareRoutingLargeZoneSwitchOnOff) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                                            makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                                            makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:81", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:82", simTime())}});

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(3));

  hostSet().healthy_hosts_ = *hosts;
  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = hosts_per_locality;
  init(true);
  updateHosts(hosts, hosts_per_locality);

  // There is only one host in the given zone for zone aware routing.
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_all_directly_.value());
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(2U, stats_.lb_zone_routing_all_directly_.value());

  // Disable runtime global zone routing.
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(false));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, ZoneAwareRoutingSmallZone) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:83", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:84", simTime())}));
  HostVectorSharedPtr local_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:0", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:1", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:2", simTime())}));

  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:81", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                             makeTestHost(info_, "tcp://127.0.0.1:82", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:83", simTime()),
                             makeTestHost(info_, "tcp://127.0.0.1:84", simTime())}});

  HostsPerLocalitySharedPtr local_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:0", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:1", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:2", simTime())}});

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(5));

  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  // There is only one host in the given zone for zone aware routing.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(100));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[0][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_sampled_.value());

  // Force request out of small zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2));
  EXPECT_EQ(hostSet().healthy_hosts_per_locality_->get()[1][0], lb_->chooseHost(nullptr));
  EXPECT_EQ(1U, stats_.lb_zone_routing_cross_zone_.value());
}

TEST_P(RoundRobinLoadBalancerTest, LowPrecisionForDistribution) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  // upstream_hosts and local_hosts do not matter, zone aware routing is based on per zone hosts.
  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime())}));
  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  HostVectorSharedPtr local_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:0", simTime())}));

  std::vector<HostVector> upstream_hosts_per_locality;
  std::vector<HostVector> local_hosts_per_locality;

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillRepeatedly(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillRepeatedly(Return(1));

  // The following host distribution with current precision should lead to the no_capacity_left
  // situation.
  // Reuse the same host in all of the structures below to reduce time test takes and this does
  // not impact load balancing logic.
  HostSharedPtr host = makeTestHost(info_, "tcp://127.0.0.1:80", simTime());
  HostVector current(45000);

  for (int i = 0; i < 45000; ++i) {
    current[i] = host;
  }
  local_hosts_per_locality.push_back(current);

  current.resize(55000);
  for (int i = 0; i < 55000; ++i) {
    current[i] = host;
  }
  local_hosts_per_locality.push_back(current);

  current.resize(44999);
  for (int i = 0; i < 44999; ++i) {
    current[i] = host;
  }
  upstream_hosts_per_locality.push_back(current);

  current.resize(55001);
  for (int i = 0; i < 55001; ++i) {
    current[i] = host;
  }
  upstream_hosts_per_locality.push_back(current);

  hostSet().healthy_hosts_per_locality_ =
      makeHostsPerLocality(std::move(upstream_hosts_per_locality));
  init(true);

  // To trigger update callback.
  auto local_hosts_per_locality_shared = makeHostsPerLocality(std::move(local_hosts_per_locality));
  updateHosts(local_hosts, local_hosts_per_locality_shared);

  // Force request out of small zone and to randomly select zone.
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(9999)).WillOnce(Return(2));
  lb_->chooseHost(nullptr);
  EXPECT_EQ(1U, stats_.lb_zone_no_capacity_left_.value());
}

TEST_P(RoundRobinLoadBalancerTest, NoZoneAwareRoutingOneZone) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime())}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:81", simTime())}});

  hostSet().healthy_hosts_ = *hosts;
  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = hosts_per_locality;
  init(true);
  updateHosts(hosts, hosts_per_locality);
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, NoZoneAwareRoutingNotHealthy) {
  HostVectorSharedPtr hosts(new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                                            makeTestHost(info_, "tcp://127.0.0.2:80", simTime())}));
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{},
                            {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                             makeTestHost(info_, "tcp://127.0.0.2:80", simTime())}});

  hostSet().healthy_hosts_ = *hosts;
  hostSet().hosts_ = *hosts;
  hostSet().healthy_hosts_per_locality_ = hosts_per_locality;
  init(true);
  updateHosts(hosts, hosts_per_locality);

  // local zone has no healthy hosts, take from the all healthy hosts.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, NoZoneAwareRoutingLocalEmpty) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime())}));
  HostVectorSharedPtr local_hosts(new HostVector({}, {}));

  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:81", simTime())}});
  HostsPerLocalitySharedPtr local_hosts_per_locality = makeHostsPerLocality({{}, {}});

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillOnce(Return(50))
      .WillOnce(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillOnce(Return(1));

  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  // Local cluster is not OK, we'll do regular routing.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(0U, stats_.lb_healthy_panic_.value());
  EXPECT_EQ(1U, stats_.lb_local_cluster_not_ok_.value());
}

TEST_P(RoundRobinLoadBalancerTest, NoZoneAwareRoutingLocalEmptyFailTrafficOnPanic) {
  common_config_.mutable_zone_aware_lb_config()->set_fail_traffic_on_panic(true);

  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime())}));
  HostVectorSharedPtr local_hosts(new HostVector({}, {}));

  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:81", simTime())}});
  HostsPerLocalitySharedPtr local_hosts_per_locality = makeHostsPerLocality({{}, {}});

  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.healthy_panic_threshold", 50))
      .WillOnce(Return(50))
      .WillOnce(Return(50));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("upstream.zone_routing.enabled", 100))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("upstream.zone_routing.min_cluster_size", 6))
      .WillOnce(Return(1));

  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  // Local cluster is not OK, we'll do regular routing (and select no host, since we're in global
  // panic).
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
  EXPECT_EQ(0U, stats_.lb_healthy_panic_.value());
  EXPECT_EQ(1U, stats_.lb_local_cluster_not_ok_.value());
}

// Validate that if we have healthy host lists >= 2, but there is no local
// locality included, that we skip zone aware routing and fallback.
TEST_P(RoundRobinLoadBalancerTest, NoZoneAwareRoutingNoLocalLocality) {
  if (&hostSet() == &failover_host_set_) { // P = 1 does not support zone-aware routing.
    return;
  }
  HostVectorSharedPtr upstream_hosts(
      new HostVector({makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime())}));
  HostVectorSharedPtr local_hosts(new HostVector({}, {}));

  HostsPerLocalitySharedPtr upstream_hosts_per_locality =
      makeHostsPerLocality({{makeTestHost(info_, "tcp://127.0.0.1:80", simTime())},
                            {makeTestHost(info_, "tcp://127.0.0.1:81", simTime())}},
                           true);
  const HostsPerLocalitySharedPtr& local_hosts_per_locality = upstream_hosts_per_locality;

  hostSet().healthy_hosts_ = *upstream_hosts;
  hostSet().hosts_ = *upstream_hosts;
  hostSet().healthy_hosts_per_locality_ = upstream_hosts_per_locality;
  init(true);
  updateHosts(local_hosts, local_hosts_per_locality);

  // Local cluster is not OK, we'll do regular routing.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(0U, stats_.lb_healthy_panic_.value());
  EXPECT_EQ(1U, stats_.lb_local_cluster_not_ok_.value());
}

INSTANTIATE_TEST_SUITE_P(PrimaryOrFailover, RoundRobinLoadBalancerTest,
                         ::testing::Values(true, false));

TEST_P(RoundRobinLoadBalancerTest, SlowStartWithDefaultParams) {
  init(false);
  const auto slow_start_window =
      EdfLoadBalancerBasePeer::slowStartWindow(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(std::chrono::milliseconds(0), slow_start_window);
  const auto aggression =
      EdfLoadBalancerBasePeer::aggression(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(1.0, aggression);
  const auto latest_host_added_time =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(std::chrono::milliseconds(0), latest_host_added_time);
  const auto slow_start_min_weight_percent =
      EdfLoadBalancerBasePeer::slowStartMinWeightPercent(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_DOUBLE_EQ(slow_start_min_weight_percent, 0.1);
}

TEST_P(RoundRobinLoadBalancerTest, SlowStartWithMinWeightPercent) {
  round_robin_lb_config_.mutable_slow_start_config()->mutable_min_weight_percent()->set_value(30);
  init(false);
  const auto slow_start_window =
      EdfLoadBalancerBasePeer::slowStartWindow(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(std::chrono::milliseconds(0), slow_start_window);
  const auto aggression =
      EdfLoadBalancerBasePeer::aggression(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(1.0, aggression);
  const auto latest_host_added_time =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(std::chrono::milliseconds(0), latest_host_added_time);
  const auto slow_start_min_weight_percent =
      EdfLoadBalancerBasePeer::slowStartMinWeightPercent(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_DOUBLE_EQ(slow_start_min_weight_percent, 0.3);
}

TEST_P(RoundRobinLoadBalancerTest, SlowStartNoWait) {
  round_robin_lb_config_.mutable_slow_start_config()->mutable_slow_start_window()->set_seconds(60);
  simTime().advanceTimeWait(std::chrono::seconds(1));
  auto host1 = makeTestHost(info_, "tcp://127.0.0.1:80", simTime());
  host_set_.hosts_ = {host1};

  init(true);

  // As no healthcheck is configured, hosts would enter slow start immediately.
  HostVector empty;
  HostVector hosts_added;
  hosts_added.push_back(host1);
  simTime().advanceTimeWait(std::chrono::seconds(5));
  hostSet().runCallbacks(hosts_added, empty);
  auto latest_host_added_time_ms =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(std::chrono::milliseconds(1000), latest_host_added_time_ms);

  // Advance time, so that host is no longer in slow start.
  simTime().advanceTimeWait(std::chrono::seconds(56));

  hosts_added.clear();
  auto host2 = makeTestHost(info_, "tcp://127.0.0.1:90", simTime());

  hosts_added.push_back(host2);

  hostSet().healthy_hosts_ = {host1, host2};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks(hosts_added, empty);

  latest_host_added_time_ms =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(std::chrono::milliseconds(62000), latest_host_added_time_ms);

  // host2 is 12 secs in slow start, the weight is scaled with time factor max(12 / 60, 0.1) = 0.2.
  simTime().advanceTimeWait(std::chrono::seconds(12));

  // Recalculate weights.
  hostSet().runCallbacks(empty, empty);

  // We expect 4:1 ratio, as host2 is in slow start mode and it's weight is scaled with
  // 0.2 factor.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));

  // host2 is 20 secs in slow start, the weight is scaled with time factor 20 / 60 == 0.33.
  simTime().advanceTimeWait(std::chrono::seconds(8));

  // Recalculate weights.
  hostSet().runCallbacks(empty, empty);

  // We expect 2:1 ratio, as host2 is in slow start mode and it's weight is scaled with
  // 0.33 factor.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));

  // Advance time, so that there are no hosts in slow start.
  simTime().advanceTimeWait(std::chrono::seconds(45));

  // Recalculate weights.
  hostSet().runCallbacks(empty, empty);

  // Now expect 1:1 ratio.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, SlowStartWaitForPassingHC) {
  round_robin_lb_config_.mutable_slow_start_config()->mutable_slow_start_window()->set_seconds(10);
  simTime().advanceTimeWait(std::chrono::seconds(1));
  auto host1 = makeTestHost(info_, "tcp://127.0.0.1:80", simTime());
  host1->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);

  host_set_.hosts_ = {host1};

  init(true);

  HostVector empty;
  HostVector hosts_added;
  hosts_added.push_back(host1);
  simTime().advanceTimeWait(std::chrono::seconds(1));
  hostSet().runCallbacks(hosts_added, empty);
  auto latest_host_added_time_ms =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(std::chrono::milliseconds(1000), latest_host_added_time_ms);

  simTime().advanceTimeWait(std::chrono::seconds(5));

  hosts_added.clear();
  auto host2 = makeTestHost(info_, "tcp://127.0.0.1:90", simTime());
  hosts_added.push_back(host2);

  hostSet().hosts_ = {host1, host2};
  hostSet().runCallbacks(hosts_added, empty);

  // As host1 has not passed first HC, it should not enter slow start mode.
  latest_host_added_time_ms =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(std::chrono::milliseconds(7000), latest_host_added_time_ms);

  simTime().advanceTimeWait(std::chrono::seconds(1));
  host1->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  hostSet().healthy_hosts_ = {host1, host2};
  // Trigger callbacks to add host1 to slow start mode.
  hostSet().runCallbacks({}, {});

  simTime().advanceTimeWait(std::chrono::seconds(1));
  host1->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  // Trigger callbacks to remove host1 from slow start mode.
  hostSet().runCallbacks({}, {});
  simTime().advanceTimeWait(std::chrono::seconds(4));
  // Trigger callbacks to remove host1 from slow start mode.
  hostSet().runCallbacks({}, {});

  // We expect 3:1 ratio, as host2 is in slow start mode, its weight is scaled with time factor
  // max(6/10, 0.1) = 0.6.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));

  // Advance time, so there are no hosts in slow start.
  simTime().advanceTimeWait(std::chrono::seconds(20));
  hostSet().runCallbacks({}, {});

  // We expect 1:1 ratio, as there are no hosts in slow start mode.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, SlowStartWithRuntimeAggression) {
  round_robin_lb_config_.mutable_slow_start_config()->mutable_slow_start_window()->set_seconds(10);
  round_robin_lb_config_.mutable_slow_start_config()->mutable_aggression()->set_runtime_key(
      "aggression");
  round_robin_lb_config_.mutable_slow_start_config()->mutable_aggression()->set_default_value(1.0);

  init(true);
  EXPECT_CALL(runtime_.snapshot_, getDouble("aggression", 1.0)).WillRepeatedly(Return(1.0));

  simTime().advanceTimeWait(std::chrono::seconds(1));

  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), 1),
                              makeTestHost(info_, "tcp://127.0.0.1:90", simTime(), 1),
                              makeTestHost(info_, "tcp://127.0.0.1:100", simTime(), 1)};

  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks({}, {});

  simTime().advanceTimeWait(std::chrono::seconds(5));
  hostSet().healthy_hosts_[0]->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  hostSet().runCallbacks({}, {});

  auto latest_host_added_time_ms =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(std::chrono::milliseconds(1000), latest_host_added_time_ms);

  // We should see 2:1:1 ratio, as hosts 2 and 3 are in slow start, their weights are scaled with
  // max(0.5,0.1)=0.5 factor.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));

  simTime().advanceTimeWait(std::chrono::seconds(4));
  HostVector hosts_added;
  auto host4 = makeTestHost(info_, "tcp://127.0.0.1:110", simTime());
  hostSet().hosts_.push_back(host4);
  hostSet().healthy_hosts_.push_back(host4);
  EXPECT_CALL(runtime_.snapshot_, getDouble("aggression", 1.0)).WillRepeatedly(Return(1.5));
  // Recompute edf schedulers.
  hostSet().runCallbacks(hosts_added, {});

  latest_host_added_time_ms =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(std::chrono::milliseconds(10000), latest_host_added_time_ms);

  // We should see 1:1:1:0 ratio, as host 2 and 3 weight is scaled with max((9/10)^(1/1.5),0.1)=0.93
  // factor, host4 weight is 1*max(0.002,0.1)=0.1.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));

  // host4 is 9 seconds in slow start, it's weight is scaled with max((9/10)^(1/1.5), 0.1)=0.93
  // factor.
  simTime().advanceTimeWait(std::chrono::seconds(9));
  hostSet().runCallbacks({}, {});

  // We should see 1:1:1:1 ratio, only host4 is in slow start with weight 0.93, and the rest of
  // hosts are outside of slow start with weight 1.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[2], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[3], lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, SlowStartNoWaitNonLinearAggression) {
  round_robin_lb_config_.mutable_slow_start_config()->mutable_slow_start_window()->set_seconds(60);
  round_robin_lb_config_.mutable_slow_start_config()->mutable_aggression()->set_runtime_key(
      "aggression");
  round_robin_lb_config_.mutable_slow_start_config()->mutable_aggression()->set_default_value(2.0);
  simTime().advanceTimeWait(std::chrono::seconds(1));

  init(true);

  // As no healthcheck is configured, hosts would enter slow start immediately.
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  simTime().advanceTimeWait(std::chrono::seconds(5));
  // Host1 is 5 secs in slow start, its weight is scaled with max((5/60)^(1/2), 0.1)=0.28 factor.
  hostSet().runCallbacks({}, {});

  // Advance time, so that host1 is no longer in slow start.
  simTime().advanceTimeWait(std::chrono::seconds(56));

  HostVector hosts_added;
  auto host2 = makeTestHost(info_, "tcp://127.0.0.1:90", simTime());

  hosts_added.push_back(host2);

  hostSet().healthy_hosts_.push_back(host2);
  hostSet().hosts_ = hostSet().healthy_hosts_;
  // host2 weight is scaled with max((0.001/60)^(1/2), 0.1)=max(0.004, 0.1)=0.1 factor.
  hostSet().runCallbacks(hosts_added, {});

  // host2 is 6 secs in slow start.
  simTime().advanceTimeWait(std::chrono::seconds(6));

  // Recalculate weights.
  hostSet().runCallbacks({}, {});

  // We expect 3:1 ratio, as host2 is 6 secs in slow start mode and it's weight is scaled with
  // max(pow(0.1, 0.5), 0.1)=0.31 factor.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));

  // host2 is 26 secs in slow start.
  simTime().advanceTimeWait(std::chrono::seconds(20));

  // Recalculate weights.
  hostSet().runCallbacks({}, {});

  // We still expect 5:3 ratio, as host2 is in slow start mode and it's weight is scaled with
  // max(pow(0.43, 0.5), 0.1)=0.65 factor.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));

  // Advance time, so that there are no hosts in slow start.
  simTime().advanceTimeWait(std::chrono::seconds(41));

  // Recalculate weights.
  hostSet().runCallbacks({}, {});

  // Now expect 1:1 ratio.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

TEST_P(RoundRobinLoadBalancerTest, SlowStartNoWaitMinWeightPercent35) {
  round_robin_lb_config_.mutable_slow_start_config()->mutable_slow_start_window()->set_seconds(60);
  round_robin_lb_config_.mutable_slow_start_config()->mutable_min_weight_percent()->set_value(35);
  simTime().advanceTimeWait(std::chrono::seconds(1));
  auto host1 = makeTestHost(info_, "tcp://127.0.0.1:80", simTime());
  host_set_.hosts_ = {host1};

  init(true);

  // As no healthcheck is configured, hosts would enter slow start immediately.
  HostVector empty;
  HostVector hosts_added;
  hosts_added.push_back(host1);
  simTime().advanceTimeWait(std::chrono::seconds(5));
  hostSet().runCallbacks(hosts_added, empty);
  auto latest_host_added_time_ms =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(std::chrono::milliseconds(1000), latest_host_added_time_ms);

  // Advance time, so that host is no longer in slow start.
  simTime().advanceTimeWait(std::chrono::seconds(56));

  hosts_added.clear();
  auto host2 = makeTestHost(info_, "tcp://127.0.0.1:90", simTime());

  hosts_added.push_back(host2);

  hostSet().healthy_hosts_ = {host1, host2};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks(hosts_added, empty);

  latest_host_added_time_ms =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(*lb_));
  EXPECT_EQ(std::chrono::milliseconds(62000), latest_host_added_time_ms);

  // host2 is 12 secs in slow start, the weight is scaled with time factor max(12 / 60, 0.35) =
  // 0.35.
  simTime().advanceTimeWait(std::chrono::seconds(12));

  // Recalculate weights.
  hostSet().runCallbacks(empty, empty);

  // We expect 5:2 ratio, as host2 is in slow start mode and it's weight is scaled with
  // 0.35 factor.
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[1,20/7]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[2,20/7]
  EXPECT_EQ(hostSet().healthy_hosts_[1],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[3,20/7]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[3,40/7]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[4,40/7]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[5,40/7]
  EXPECT_EQ(hostSet().healthy_hosts_[1],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[6,40/7]

  // host2 is 30 secs in slow start, the weight is scaled with time factor max(30 / 60, 0.35) ==
  // 0.5.
  simTime().advanceTimeWait(std::chrono::seconds(18));

  // Recalculate weights.
  hostSet().runCallbacks(empty, empty);

  // We expect 2:1 ratio, as host2 is in slow start mode and it's weight is scaled with
  // 0.5 factor.
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[1,2]
  EXPECT_EQ(hostSet().healthy_hosts_[1],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[2,2]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[2,4]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[3,4]
  EXPECT_EQ(hostSet().healthy_hosts_[1],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[4,4]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[4,6]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_->chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[5,6]

  // Advance time, so that there are no hosts in slow start.
  simTime().advanceTimeWait(std::chrono::seconds(45));

  // Recalculate weights.
  hostSet().runCallbacks(empty, empty);

  // Now expect 1:1 ratio.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

class LeastRequestLoadBalancerTest : public LoadBalancerTestBase {
public:
  LeastRequestLoadBalancer lb_{
      priority_set_, nullptr, stats_, runtime_, random_, common_config_, least_request_lb_config_,
      simTime()};
};

TEST_P(LeastRequestLoadBalancerTest, NoHosts) { EXPECT_EQ(nullptr, lb_.chooseHost(nullptr)); }

TEST_P(LeastRequestLoadBalancerTest, SingleHost) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks({}, {}); // Trigger callbacks. The added/removed lists are not relevant.

  // Host weight is 1.
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(2)).WillOnce(Return(3));
    EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));
  }

  // Host weight is 100.
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(2)).WillOnce(Return(3));
    EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));
  }

  HostVector empty;
  {
    hostSet().runCallbacks(empty, empty);
    EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(2)).WillOnce(Return(3));
    EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));
  }

  {
    HostVector remove_hosts;
    remove_hosts.push_back(hostSet().hosts_[0]);
    hostSet().healthy_hosts_.clear();
    hostSet().hosts_.clear();
    hostSet().runCallbacks(empty, remove_hosts);
    EXPECT_CALL(random_, random()).WillOnce(Return(0));
    EXPECT_EQ(nullptr, lb_.chooseHost(nullptr));
  }
}

TEST_P(LeastRequestLoadBalancerTest, Normal) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks({}, {}); // Trigger callbacks. The added/removed lists are not relevant.

  hostSet().healthy_hosts_[0]->stats().rq_active_.set(1);
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(2);
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(2)).WillOnce(Return(3));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));

  hostSet().healthy_hosts_[0]->stats().rq_active_.set(2);
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(1);
  EXPECT_CALL(random_, random()).WillOnce(Return(0)).WillOnce(Return(2)).WillOnce(Return(3));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
}

TEST_P(LeastRequestLoadBalancerTest, PNC) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:83", simTime())};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks({}, {}); // Trigger callbacks. The added/removed lists are not relevant.

  hostSet().healthy_hosts_[0]->stats().rq_active_.set(4);
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(3);
  hostSet().healthy_hosts_[2]->stats().rq_active_.set(2);
  hostSet().healthy_hosts_[3]->stats().rq_active_.set(1);

  // Creating various load balancer objects with different choice configs.
  envoy::config::cluster::v3::Cluster::LeastRequestLbConfig lr_lb_config;
  lr_lb_config.mutable_choice_count()->set_value(2);
  LeastRequestLoadBalancer lb_2{priority_set_, nullptr,        stats_,       runtime_,
                                random_,       common_config_, lr_lb_config, simTime()};
  lr_lb_config.mutable_choice_count()->set_value(5);
  LeastRequestLoadBalancer lb_5{priority_set_, nullptr,        stats_,       runtime_,
                                random_,       common_config_, lr_lb_config, simTime()};

  // Verify correct number of choices.

  // 0 choices configured should default to P2C.
  EXPECT_CALL(random_, random()).Times(3).WillRepeatedly(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));

  // 2 choices configured results in P2C.
  EXPECT_CALL(random_, random()).Times(3).WillRepeatedly(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));

  // 5 choices configured results in P5C.
  EXPECT_CALL(random_, random()).Times(6).WillRepeatedly(Return(0));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_5.chooseHost(nullptr));

  // Verify correct host chosen in P5C scenario.
  EXPECT_CALL(random_, random())
      .Times(6)
      .WillOnce(Return(0))
      .WillOnce(Return(3))
      .WillOnce(Return(0))
      .WillOnce(Return(3))
      .WillOnce(Return(2))
      .WillOnce(Return(1));
  EXPECT_EQ(hostSet().healthy_hosts_[3], lb_5.chooseHost(nullptr));
}

TEST_P(LeastRequestLoadBalancerTest, WeightImbalance) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), 1),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), 2)};

  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks({}, {}); // Trigger callbacks. The added/removed lists are not relevant.

  EXPECT_CALL(random_, random()).WillRepeatedly(Return(0));

  // We should see 2:1 ratio for hosts[1] to hosts[0].
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));

  // Bringing hosts[1] to an active request should yield a 1:1 ratio.
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(1);
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));

  // Settings hosts[0] to an active request and hosts[1] to no active requests should yield a 4:1
  // ratio.
  hostSet().healthy_hosts_[0]->stats().rq_active_.set(1);
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(0);
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));
}

// Validate that the load balancer defaults to an active request bias value of 1.0 if the runtime
// value is invalid (less than 0.0).
TEST_P(LeastRequestLoadBalancerTest, WeightImbalanceWithInvalidActiveRequestBias) {
  envoy::config::cluster::v3::Cluster::LeastRequestLbConfig lr_lb_config;
  lr_lb_config.mutable_active_request_bias()->set_runtime_key("ar_bias");
  lr_lb_config.mutable_active_request_bias()->set_default_value(1.0);
  LeastRequestLoadBalancer lb_2{priority_set_, nullptr,        stats_,       runtime_,
                                random_,       common_config_, lr_lb_config, simTime()};

  EXPECT_CALL(runtime_.snapshot_, getDouble("ar_bias", 1.0)).WillRepeatedly(Return(-1.0));

  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), 1),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), 2)};

  hostSet().hosts_ = hostSet().healthy_hosts_;

  // Trigger callbacks. The added/removed lists are not relevant.
  EXPECT_LOG_CONTAINS(
      "warn", "upstream: invalid active request bias supplied (runtime key ar_bias), using 1.0",
      hostSet().runCallbacks({}, {}));

  EXPECT_CALL(random_, random()).WillRepeatedly(Return(0));

  // We should see 2:1 ratio for hosts[1] to hosts[0].
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));

  // Bringing hosts[1] to an active request should yield a 1:1 ratio.
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(1);
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));

  // Settings hosts[0] to an active request and hosts[1] to no active requests should yield a 4:1
  // ratio.
  hostSet().healthy_hosts_[0]->stats().rq_active_.set(1);
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(0);
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
}

TEST_P(LeastRequestLoadBalancerTest, WeightImbalanceWithCustomActiveRequestBias) {
  // Create a load balancer with a custom active request bias.
  envoy::config::cluster::v3::Cluster::LeastRequestLbConfig lr_lb_config;
  lr_lb_config.mutable_active_request_bias()->set_runtime_key("ar_bias");
  lr_lb_config.mutable_active_request_bias()->set_default_value(1.0);
  LeastRequestLoadBalancer lb_2{priority_set_, nullptr,        stats_,       runtime_,
                                random_,       common_config_, lr_lb_config, simTime()};

  EXPECT_CALL(runtime_.snapshot_, getDouble("ar_bias", 1.0)).WillRepeatedly(Return(0.0));

  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), 1),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), 2)};

  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks({}, {}); // Trigger callbacks. The added/removed lists are not relevant.

  EXPECT_CALL(random_, random()).WillRepeatedly(Return(0));

  // We should see 2:1 ratio for hosts[1] to hosts[0], regardless of the active request count.
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(1);
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
}

TEST_P(LeastRequestLoadBalancerTest, WeightImbalanceCallbacks) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime(), 1),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime(), 2)};

  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks({}, {}); // Trigger callbacks. The added/removed lists are not relevant.

  EXPECT_CALL(random_, random()).WillRepeatedly(Return(0));

  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));

  // Remove and verify we get other host.
  HostVector empty;
  HostVector hosts_removed;
  hosts_removed.push_back(hostSet().hosts_[1]);
  hostSet().hosts_.erase(hostSet().hosts_.begin() + 1);
  hostSet().healthy_hosts_.erase(hostSet().healthy_hosts_.begin() + 1);
  hostSet().runCallbacks(empty, hosts_removed);
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_.chooseHost(nullptr));
}

TEST_P(LeastRequestLoadBalancerTest, SlowStartWithDefaultParams) {
  envoy::config::cluster::v3::Cluster::LeastRequestLbConfig lr_lb_config;
  LeastRequestLoadBalancer lb_2{priority_set_, nullptr,        stats_,       runtime_,
                                random_,       common_config_, lr_lb_config, simTime()};
  const auto slow_start_window =
      EdfLoadBalancerBasePeer::slowStartWindow(static_cast<EdfLoadBalancerBase&>(lb_2));
  EXPECT_EQ(std::chrono::milliseconds(0), slow_start_window);
  const auto aggression =
      EdfLoadBalancerBasePeer::aggression(static_cast<EdfLoadBalancerBase&>(lb_2));
  EXPECT_EQ(1.0, aggression);
  const auto latest_host_added_time =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(lb_2));
  EXPECT_EQ(std::chrono::milliseconds(0), latest_host_added_time);
  const auto slow_start_min_weight_percent =
      EdfLoadBalancerBasePeer::slowStartMinWeightPercent(static_cast<EdfLoadBalancerBase&>(lb_2));
  EXPECT_DOUBLE_EQ(slow_start_min_weight_percent, 0.1);
}

TEST_P(LeastRequestLoadBalancerTest, SlowStartNoWait) {
  envoy::config::cluster::v3::Cluster::LeastRequestLbConfig lr_lb_config;
  lr_lb_config.mutable_slow_start_config()->mutable_slow_start_window()->set_seconds(60);
  lr_lb_config.mutable_active_request_bias()->set_runtime_key("ar_bias");
  lr_lb_config.mutable_active_request_bias()->set_default_value(1.0);
  LeastRequestLoadBalancer lb_2{priority_set_, nullptr,        stats_,       runtime_,
                                random_,       common_config_, lr_lb_config, simTime()};
  simTime().advanceTimeWait(std::chrono::seconds(1));

  // As no healthcheck is configured, hosts would enter slow start immediately.
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  simTime().advanceTimeWait(std::chrono::seconds(5));
  // Host1 is 5 secs in slow start, its weight is scaled with max((5/60)^1, 0.1)=0.1 factor.
  hostSet().runCallbacks({}, {});

  auto latest_host_added_time =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(lb_2));
  EXPECT_EQ(std::chrono::milliseconds(1000), latest_host_added_time);

  // Advance time, so that host is no longer in slow start.
  simTime().advanceTimeWait(std::chrono::seconds(56));

  auto host2 = makeTestHost(info_, "tcp://127.0.0.1:90", simTime());
  hostSet().healthy_hosts_.push_back(host2);
  hostSet().hosts_ = hostSet().healthy_hosts_;
  HostVector hosts_added;
  hosts_added.push_back(host2);

  hostSet().runCallbacks(hosts_added, {});

  latest_host_added_time =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(lb_2));
  EXPECT_EQ(std::chrono::milliseconds(62000), latest_host_added_time);

  // host2 is 20 secs in slow start, the weight is scaled with time factor max(20/60, 0.1) = 0.16.
  simTime().advanceTimeWait(std::chrono::seconds(10));

  // Recalculate weights.
  hostSet().runCallbacks({}, {});

  hostSet().healthy_hosts_[0]->stats().rq_active_.set(1);
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(0);

  // We expect 3:1 ratio, as host2 is in slow start mode and it's weight is scaled with
  // 0.16 factor and host1 weight with 0.5 factor (due to active request bias).
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));

  // host2 is 50 secs in slow start, the weight is scaled with time factor max(40/60, 0.1) = 0.66.
  simTime().advanceTimeWait(std::chrono::seconds(30));

  // Recalculate weights.
  hostSet().runCallbacks({}, {});

  // We expect 4:3 ratio, as host2 is in slow start mode and it's weight is scaled with
  // 0.66 factor and host1 weight with 0.5 factor.
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
}

TEST_P(LeastRequestLoadBalancerTest, SlowStartWaitForPassingHC) {
  envoy::config::cluster::v3::Cluster::LeastRequestLbConfig lr_lb_config;
  lr_lb_config.mutable_slow_start_config()->mutable_slow_start_window()->set_seconds(10);
  lr_lb_config.mutable_slow_start_config()->mutable_aggression()->set_runtime_key("aggression");
  lr_lb_config.mutable_slow_start_config()->mutable_aggression()->set_default_value(0.9);
  lr_lb_config.mutable_active_request_bias()->set_runtime_key("ar_bias");
  lr_lb_config.mutable_active_request_bias()->set_default_value(0.9);

  LeastRequestLoadBalancer lb_2{priority_set_, nullptr,        stats_,       runtime_,
                                random_,       common_config_, lr_lb_config, simTime()};

  simTime().advanceTimeWait(std::chrono::seconds(1));
  auto host1 = makeTestHost(info_, "tcp://127.0.0.1:80", simTime());
  host1->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);

  host_set_.hosts_ = {host1};

  HostVector hosts_added;
  hosts_added.push_back(host1);
  simTime().advanceTimeWait(std::chrono::seconds(1));
  hostSet().runCallbacks(hosts_added, {});

  auto latest_host_added_time =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(lb_2));
  EXPECT_EQ(std::chrono::milliseconds(0), latest_host_added_time);

  simTime().advanceTimeWait(std::chrono::seconds(5));

  hosts_added.clear();
  auto host2 = makeTestHost(info_, "tcp://127.0.0.1:90", simTime());
  hosts_added.push_back(host2);

  hostSet().healthy_hosts_ = {host1, host2};
  hostSet().hosts_ = hostSet().healthyHosts();
  hostSet().runCallbacks(hosts_added, {});

  latest_host_added_time =
      EdfLoadBalancerBasePeer::latestHostAddedTime(static_cast<EdfLoadBalancerBase&>(lb_2));
  EXPECT_EQ(std::chrono::milliseconds(7000), latest_host_added_time);

  simTime().advanceTimeWait(std::chrono::seconds(1));
  host1->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  hostSet().healthy_hosts_ = {host1, host2};

  hostSet().healthy_hosts_[0]->stats().rq_active_.set(1);
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(0);

  hostSet().healthy_hosts_ = {host1, host2};
  hostSet().hosts_ = hostSet().healthyHosts();

  // Trigger callbacks to add host1 to slow start mode.
  hostSet().runCallbacks({}, {});

  // We expect 11:2 ratio, as host2 is in slow start mode, its weight is scaled with factor
  // max(pow(0.1, 1.11), 0.1)=0.1. Host1 is 7 seconds in slow start and its weight is scaled with
  // active request and time bias 0.53 * max(pow(0.7, 1.11), 0.1) = 0.36.

  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_2.chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[25/9, 10]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_2.chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[50/9, 10]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_2.chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[75/9, 10]
  EXPECT_EQ(hostSet().healthy_hosts_[1],
            lb_2.chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[100/9, 10]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_2.chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[100/9, 20]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_2.chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[125/9, 20]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_2.chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[150/9, 20]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_2.chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[175/9, 20]
  EXPECT_EQ(hostSet().healthy_hosts_[1],
            lb_2.chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[200/9, 20]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_2.chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[200/9, 30]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_2.chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[225/9, 30]
  EXPECT_EQ(hostSet().healthy_hosts_[0],
            lb_2.chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[250/9, 30]
  EXPECT_EQ(hostSet().healthy_hosts_[1],
            lb_2.chooseHost(nullptr)); // before choose: edf.deadline[host1,host2]=[275/9, 30]

  simTime().advanceTimeWait(std::chrono::seconds(3));
  host1->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  // Trigger callbacks to remove host1 from slow start mode.
  hostSet().runCallbacks({}, {});

  // We expect 3:5 ratio, as host2 is 4 seconds in slow start, its weight is scaled with factor
  // max(pow(0.4, 1.11), 0.1)=0.36. Host1 is not in slow start and its weight is scaled with active
  // request bias = 0.53.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));

  // Host2 is 7 seconds in slow start, the weight is scaled with time factor 7 / 10 == 0.7.
  simTime().advanceTimeWait(std::chrono::seconds(3));

  hostSet().runCallbacks({}, {});

  // We expect 6:5 ratio, as host2 is in slow start mode, its weight is scaled with time factor
  // max(pow(0.7, 1.11), 0.1)=0.67. Host1 weight is scaled with active request bias = 0.53.
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
}

INSTANTIATE_TEST_SUITE_P(PrimaryOrFailover, LeastRequestLoadBalancerTest,
                         ::testing::Values(true, false));

class RandomLoadBalancerTest : public LoadBalancerTestBase {
public:
  void init() {
    lb_ = std::make_shared<RandomLoadBalancer>(priority_set_, nullptr, stats_, runtime_, random_,
                                               common_config_);
  }
  std::shared_ptr<LoadBalancer> lb_;
};

TEST_P(RandomLoadBalancerTest, NoHosts) {
  init();

  EXPECT_EQ(nullptr, lb_->peekAnotherHost(nullptr));
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
}

TEST_P(RandomLoadBalancerTest, Normal) {
  init();
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks({}, {}); // Trigger callbacks. The added/removed lists are not relevant.

  EXPECT_CALL(random_, random()).WillOnce(Return(2));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->peekAnotherHost(nullptr));

  EXPECT_CALL(random_, random()).WillOnce(Return(3));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->peekAnotherHost(nullptr));

  EXPECT_CALL(random_, random()).Times(0);
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr));
}

TEST_P(RandomLoadBalancerTest, FailClusterOnPanic) {
  common_config_.mutable_zone_aware_lb_config()->set_fail_traffic_on_panic(true);
  init();

  hostSet().healthy_hosts_ = {};
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  hostSet().runCallbacks({}, {}); // Trigger callbacks. The added/removed lists are not relevant.
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr));
}

INSTANTIATE_TEST_SUITE_P(PrimaryOrFailover, RandomLoadBalancerTest, ::testing::Values(true, false));

TEST(LoadBalancerSubsetInfoImplTest, DefaultConfigIsDiabled) {
  auto subset_info = LoadBalancerSubsetInfoImpl(
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::default_instance());

  EXPECT_FALSE(subset_info.isEnabled());
  EXPECT_TRUE(subset_info.fallbackPolicy() ==
              envoy::config::cluster::v3::Cluster::LbSubsetConfig::NO_FALLBACK);
  EXPECT_EQ(subset_info.defaultSubset().fields_size(), 0);
  EXPECT_EQ(subset_info.subsetSelectors().size(), 0);
}

TEST(LoadBalancerSubsetInfoImplTest, SubsetConfig) {
  auto subset_value = ProtobufWkt::Value();
  subset_value.set_string_value("the value");

  auto subset_config = envoy::config::cluster::v3::Cluster::LbSubsetConfig::default_instance();
  subset_config.set_fallback_policy(
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::DEFAULT_SUBSET);
  subset_config.mutable_default_subset()->mutable_fields()->insert({"key", subset_value});
  auto subset_selector1 = subset_config.mutable_subset_selectors()->Add();
  subset_selector1->add_keys("selector_key1");
  auto subset_selector2 = subset_config.mutable_subset_selectors()->Add();
  subset_selector2->add_keys("selector_key2");
  subset_selector2->set_fallback_policy(
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::ANY_ENDPOINT);

  auto subset_info = LoadBalancerSubsetInfoImpl(subset_config);

  EXPECT_TRUE(subset_info.isEnabled());
  EXPECT_TRUE(subset_info.fallbackPolicy() ==
              envoy::config::cluster::v3::Cluster::LbSubsetConfig::DEFAULT_SUBSET);
  EXPECT_EQ(subset_info.defaultSubset().fields_size(), 1);
  EXPECT_EQ(subset_info.defaultSubset().fields().at("key").string_value(),
            std::string("the value"));
  EXPECT_EQ(subset_info.subsetSelectors().size(), 2);
  EXPECT_EQ(subset_info.subsetSelectors()[0]->selectorKeys(),
            std::set<std::string>({"selector_key1"}));
  EXPECT_EQ(subset_info.subsetSelectors()[0]->fallbackPolicy(),
            envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::NOT_DEFINED);
  EXPECT_EQ(subset_info.subsetSelectors()[1]->selectorKeys(),
            std::set<std::string>({"selector_key2"}));
  EXPECT_EQ(subset_info.subsetSelectors()[1]->fallbackPolicy(),
            envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::ANY_ENDPOINT);
}

TEST(LoadBalancerSubsetInfoImplTest, KeysSubsetFallbackValid) {
  auto subset_config = envoy::config::cluster::v3::Cluster::LbSubsetConfig::default_instance();
  auto selector1 = subset_config.mutable_subset_selectors()->Add();
  selector1->add_keys("key1");
  selector1->add_keys("key2");
  selector1->add_keys("key3");
  selector1->set_fallback_policy(
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET);
  selector1->add_fallback_keys_subset("key1");
  selector1->add_fallback_keys_subset("key3");

  auto selector2 = subset_config.mutable_subset_selectors()->Add();
  selector2->add_keys("key1");
  selector2->add_keys("key3");
  selector2->add_keys("key4");
  selector2->set_fallback_policy(
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET);
  selector2->add_fallback_keys_subset("key4");

  auto subset_info = LoadBalancerSubsetInfoImpl(subset_config);

  EXPECT_EQ(subset_info.subsetSelectors()[0]->fallbackPolicy(),
            envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET);
  EXPECT_EQ(subset_info.subsetSelectors()[0]->selectorKeys(),
            std::set<std::string>({"key1", "key2", "key3"}));
  EXPECT_EQ(subset_info.subsetSelectors()[0]->fallbackKeysSubset(),
            std::set<std::string>({"key1", "key3"}));

  EXPECT_EQ(subset_info.subsetSelectors()[1]->fallbackPolicy(),
            envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET);
  EXPECT_EQ(subset_info.subsetSelectors()[1]->selectorKeys(),
            std::set<std::string>({"key1", "key3", "key4"}));
  EXPECT_EQ(subset_info.subsetSelectors()[1]->fallbackKeysSubset(),
            std::set<std::string>({"key4"}));
}

TEST(LoadBalancerSubsetInfoImplTest, KeysSubsetForOtherPolicyInvalid) {
  auto subset_config = envoy::config::cluster::v3::Cluster::LbSubsetConfig::default_instance();
  auto selector = subset_config.mutable_subset_selectors()->Add();

  selector->add_keys("key1");
  selector->add_keys("key2");
  selector->set_fallback_policy(
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::ANY_ENDPOINT);
  selector->add_fallback_keys_subset("key1");

  EXPECT_THROW_WITH_MESSAGE(LoadBalancerSubsetInfoImpl{subset_config}, EnvoyException,
                            "fallback_keys_subset can be set only for KEYS_SUBSET fallback_policy");
}

TEST(LoadBalancerSubsetInfoImplTest, KeysSubsetNotASubsetInvalid) {
  auto subset_config = envoy::config::cluster::v3::Cluster::LbSubsetConfig::default_instance();
  auto selector = subset_config.mutable_subset_selectors()->Add();

  selector->add_keys("key1");
  selector->add_keys("key2");
  selector->set_fallback_policy(
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET);
  selector->add_fallback_keys_subset("key3");

  EXPECT_THROW_WITH_MESSAGE(LoadBalancerSubsetInfoImpl{subset_config}, EnvoyException,
                            "fallback_keys_subset must be a subset of selector keys");
}

TEST(LoadBalancerSubsetInfoImplTest, KeysSubsetEmptyInvalid) {
  auto subset_config = envoy::config::cluster::v3::Cluster::LbSubsetConfig::default_instance();
  auto selector = subset_config.mutable_subset_selectors()->Add();

  selector->add_keys("key1");
  selector->add_keys("key2");
  selector->set_fallback_policy(
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET);

  EXPECT_THROW_WITH_MESSAGE(LoadBalancerSubsetInfoImpl{subset_config}, EnvoyException,
                            "fallback_keys_subset cannot be empty");
}

TEST(LoadBalancerSubsetInfoImplTest, KeysSubsetEqualKeysInvalid) {
  auto subset_config = envoy::config::cluster::v3::Cluster::LbSubsetConfig::default_instance();
  auto selector = subset_config.mutable_subset_selectors()->Add();

  selector->add_keys("key1");
  selector->add_keys("key2");
  selector->set_fallback_policy(
      envoy::config::cluster::v3::Cluster::LbSubsetConfig::LbSubsetSelector::KEYS_SUBSET);
  selector->add_fallback_keys_subset("key2");
  selector->add_fallback_keys_subset("key1");

  EXPECT_THROW_WITH_MESSAGE(LoadBalancerSubsetInfoImpl{subset_config}, EnvoyException,
                            "fallback_keys_subset cannot be equal to keys");
}

TEST(LoadBalancerContextBaseTest, LoadBalancerContextBaseTest) {
  {
    LoadBalancerContextBase context;
    MockPrioritySet mock_priority_set;
    HealthyAndDegradedLoad priority_load{Upstream::HealthyLoad({100, 0, 0}),
                                         Upstream::DegradedLoad({0, 0, 0})};
    RetryPriority::PriorityMappingFunc empty_func =
        [](const Upstream::HostDescription&) -> absl::optional<uint32_t> { return absl::nullopt; };
    MockHost mock_host;

    EXPECT_EQ(absl::nullopt, context.computeHashKey());
    EXPECT_EQ(nullptr, context.downstreamConnection());
    EXPECT_EQ(nullptr, context.metadataMatchCriteria());
    EXPECT_EQ(nullptr, context.downstreamHeaders());

    EXPECT_EQ(&priority_load,
              &(context.determinePriorityLoad(mock_priority_set, priority_load, empty_func)));
    EXPECT_EQ(false, context.shouldSelectAnotherHost(mock_host));
    EXPECT_EQ(1, context.hostSelectionRetryCount());
    EXPECT_EQ(nullptr, context.upstreamSocketOptions());
    EXPECT_EQ(nullptr, context.upstreamTransportSocketOptions());
    EXPECT_EQ(absl::nullopt, context.overrideHostToSelect());
  }
}

} // namespace
} // namespace Upstream
} // namespace Envoy
