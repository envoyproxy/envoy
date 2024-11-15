#include "source/extensions/load_balancing_policies/least_request/least_request_lb.h"

#include "test/extensions/load_balancing_policies/common/load_balancer_impl_base_test.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

using testing::NiceMock;
using testing::Return;

class LeastRequestLoadBalancerTest : public LoadBalancerTestBase {
public:
  LeastRequestLoadBalancer lb_{
      priority_set_, nullptr, stats_, runtime_, random_, common_config_, least_request_lb_config_,
      simTime()};
};

TEST_P(LeastRequestLoadBalancerTest, NoHosts) { EXPECT_EQ(nullptr, lb_.chooseHost(nullptr)); }

TEST_P(LeastRequestLoadBalancerTest, SingleHostAndPeek) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks({}, {}); // Trigger callbacks. The added/removed lists are not relevant.

  // Host weight is 1 and peek.
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(0));
    EXPECT_EQ(nullptr, lb_.peekAnotherHost(nullptr));
  }
}

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

TEST_P(LeastRequestLoadBalancerTest, DefaultSelectionMethod) {
  envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest lr_lb_config;
  EXPECT_EQ(lr_lb_config.selection_method(),
            envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest::N_CHOICES);
}

TEST_P(LeastRequestLoadBalancerTest, FullScanOneHostWithLeastRequests) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:83", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:84", simTime())};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks({}, {}); // Trigger callbacks. The added/removed lists are not relevant.

  hostSet().healthy_hosts_[0]->stats().rq_active_.set(4);
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(3);
  hostSet().healthy_hosts_[2]->stats().rq_active_.set(2);
  hostSet().healthy_hosts_[3]->stats().rq_active_.set(1);
  hostSet().healthy_hosts_[4]->stats().rq_active_.set(5);

  envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest lr_lb_config;

  // Enable FULL_SCAN on hosts.
  lr_lb_config.set_selection_method(
      envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest::FULL_SCAN);

  LeastRequestLoadBalancer lb{priority_set_, nullptr, stats_,       runtime_,
                              random_,       1,       lr_lb_config, simTime()};

  // With FULL_SCAN we will always choose the host with least number of active requests.
  EXPECT_EQ(hostSet().healthy_hosts_[3], lb.chooseHost(nullptr));
}

TEST_P(LeastRequestLoadBalancerTest, FullScanMultipleHostsWithLeastRequests) {
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:81", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:82", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:83", simTime()),
                              makeTestHost(info_, "tcp://127.0.0.1:84", simTime())};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks({}, {}); // Trigger callbacks. The added/removed lists are not relevant.

  hostSet().healthy_hosts_[0]->stats().rq_active_.set(3);
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(3);
  hostSet().healthy_hosts_[2]->stats().rq_active_.set(1);
  hostSet().healthy_hosts_[3]->stats().rq_active_.set(1);
  hostSet().healthy_hosts_[4]->stats().rq_active_.set(1);

  envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest lr_lb_config;

  // Enable FULL_SCAN on hosts.
  lr_lb_config.set_selection_method(
      envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest::FULL_SCAN);

  auto random = Random::RandomGeneratorImpl();

  LeastRequestLoadBalancer lb{priority_set_, nullptr, stats_,       runtime_,
                              random,        1,       lr_lb_config, simTime()};

  // Make 1 million selections. Then, check that the selection probability is
  // approximately equal among the 3 hosts tied for least requests.
  // Accept a +/-0.5% deviation from the expected selection probability (33.3..%).
  size_t num_selections = 1000000;
  size_t expected_approx_selections_per_tied_host = num_selections / 3;
  size_t abs_error = 5000;

  size_t host_2_counts = 0;
  size_t host_3_counts = 0;
  size_t host_4_counts = 0;

  for (size_t i = 0; i < num_selections; ++i) {
    auto selected_host = lb.chooseHost(nullptr);

    if (selected_host == hostSet().healthy_hosts_[2]) {
      ++host_2_counts;
    } else if (selected_host == hostSet().healthy_hosts_[3]) {
      ++host_3_counts;
    } else if (selected_host == hostSet().healthy_hosts_[4]) {
      ++host_4_counts;
    } else {
      FAIL() << "Must only select hosts with least requests";
    }
  }

  EXPECT_NEAR(expected_approx_selections_per_tied_host, host_2_counts, abs_error);
  EXPECT_NEAR(expected_approx_selections_per_tied_host, host_3_counts, abs_error);
  EXPECT_NEAR(expected_approx_selections_per_tied_host, host_4_counts, abs_error);
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
  hostSet().runCallbacks({}, {});
  // Host1 is 5 secs in slow start, its weight is scaled with max((5/60)^1, 0.1)=0.1 factor.
  simTime().advanceTimeWait(std::chrono::seconds(5));

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

  // host2 is 10 secs in slow start, the weight is scaled with time factor max(10/60, 0.1) = 0.16.
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

  // host2 is 40 secs in slow start, the weight is scaled with time factor max(40/60, 0.1) = 0.66.
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

TEST_P(LeastRequestLoadBalancerTest, SlowStartWithActiveHC) {
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
  host1->setLastHcPassTime(simTime().monotonicTime());
  hostSet().healthy_hosts_ = {host1, host2};

  hostSet().healthy_hosts_[0]->stats().rq_active_.set(1);
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(0);
  hostSet().hosts_ = hostSet().healthyHosts();
  simTime().advanceTimeWait(std::chrono::seconds(7));
  // Trigger callbacks to add host1 to slow start mode.
  hostSet().runCallbacks({}, {});
  // We expect 9:4 ratio, as host1 is 7 sec in slow start mode, its weight is scaled with active
  // request bias and time factor 0.53 * max(pow(0.7, 1.11), 0.1)=0.35. Host2 is 8 seconds in slow
  // start and its weight is scaled with time bias max(pow(0.7, 1.11), 0.1) = 0.78.

  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));

  simTime().advanceTimeWait(std::chrono::seconds(1));
  host2->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  // Trigger callbacks to remove host2 from slow start mode.
  hostSet().runCallbacks({}, {});
  hostSet().healthy_hosts_[0]->stats().rq_active_.set(1);
  hostSet().healthy_hosts_[1]->stats().rq_active_.set(2);

  // We expect 6:5 ratio, as host1 is 8 seconds in slow start, its weight is scaled with active
  // request bias and time factor 0.53 * max(pow(0.8, 1.11), 0.1)=0.41. Host2 is not in slow start
  // and its weight is scaled with active request bias = 0.37.
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));

  EXPECT_CALL(runtime_.snapshot_, getDouble("aggression", 0.9)).WillRepeatedly(Return(1.0));
  EXPECT_CALL(runtime_.snapshot_, getDouble("ar_bias", 0.9)).WillRepeatedly(Return(1.0));
  simTime().advanceTimeWait(std::chrono::seconds(20));
  host2->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  host2->setLastHcPassTime(simTime().monotonicTime());
  hostSet().healthy_hosts_ = {host1, host2};
  hostSet().hosts_ = hostSet().healthyHosts();
  simTime().advanceTimeWait(std::chrono::seconds(5));
  hostSet().healthy_hosts_[0]->stats().rq_active_.dec();
  hostSet().healthy_hosts_[1]->stats().rq_active_.dec();
  hostSet().healthy_hosts_[1]->stats().rq_active_.dec();
  // Trigger callbacks to remove host1 from slow start. Host2 re-enters slow start due to HC pass.
  hostSet().runCallbacks({}, {});
  // We expect 2:1 ratio, as host2 is in slow start mode, its weight is scaled with time factor
  // max(pow(0.5, 1), 0.1)= 0.5. Host1 weight is 1.
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_2.chooseHost(nullptr));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_2.chooseHost(nullptr));
}

INSTANTIATE_TEST_SUITE_P(PrimaryOrFailoverAndLegacyOrNew, LeastRequestLoadBalancerTest,
                         ::testing::Values(LoadBalancerTestParam{true},
                                           LoadBalancerTestParam{false}));

} // namespace
} // namespace Upstream
} // namespace Envoy
