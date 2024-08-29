#include "source/extensions/load_balancing_policies/random/random_lb.h"

#include "test/extensions/load_balancing_policies/common/load_balancer_impl_base_test.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

using testing::Return;

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

INSTANTIATE_TEST_SUITE_P(PrimaryOrFailoverAndLegacyOrNew, RandomLoadBalancerTest,
                         ::testing::Values(LoadBalancerTestParam{true},
                                           LoadBalancerTestParam{false}));

} // namespace
} // namespace Upstream
} // namespace Envoy
