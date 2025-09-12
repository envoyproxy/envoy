#include "source/extensions/load_balancing_policies/random/config.h"
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
                                               50, config_);
  }

  envoy::extensions::load_balancing_policies::random::v3::Random config_;
  std::shared_ptr<LoadBalancer> lb_;
};

TEST_P(RandomLoadBalancerTest, NoHosts) {
  init();

  EXPECT_EQ(nullptr, lb_->peekAnotherHost(nullptr));
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr).host);
}

TEST_P(RandomLoadBalancerTest, Normal) {
  init();
  hostSet().healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80"),
                              makeTestHost(info_, "tcp://127.0.0.1:81")};
  hostSet().hosts_ = hostSet().healthy_hosts_;
  hostSet().runCallbacks({}, {}); // Trigger callbacks. The added/removed lists are not relevant.

  EXPECT_CALL(random_, random()).WillOnce(Return(2));
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->peekAnotherHost(nullptr));

  EXPECT_CALL(random_, random()).WillOnce(Return(3));
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->peekAnotherHost(nullptr));

  EXPECT_CALL(random_, random()).Times(0);
  EXPECT_EQ(hostSet().healthy_hosts_[0], lb_->chooseHost(nullptr).host);
  EXPECT_EQ(hostSet().healthy_hosts_[1], lb_->chooseHost(nullptr).host);
}

TEST_P(RandomLoadBalancerTest, FailClusterOnPanic) {
  config_.mutable_locality_lb_config()->mutable_zone_aware_lb_config()->set_fail_traffic_on_panic(
      true);
  init();

  hostSet().healthy_hosts_ = {};
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80"),
                      makeTestHost(info_, "tcp://127.0.0.1:81")};
  hostSet().runCallbacks({}, {}); // Trigger callbacks. The added/removed lists are not relevant.
  EXPECT_EQ(nullptr, lb_->chooseHost(nullptr).host);
}

INSTANTIATE_TEST_SUITE_P(PrimaryOrFailoverAndLegacyOrNew, RandomLoadBalancerTest,
                         ::testing::Values(LoadBalancerTestParam{true},
                                           LoadBalancerTestParam{false}));

TEST(TypedRandomLbConfigTest, TypedRandomLbConfigTest) {
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig common;

    Extensions::LoadBalancingPolices::Random::TypedRandomLbConfig typed_config(common);

    EXPECT_FALSE(typed_config.lb_config_.has_locality_lb_config());
  }

  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig common;

    common.mutable_locality_weighted_lb_config();

    Extensions::LoadBalancingPolices::Random::TypedRandomLbConfig typed_config(common);

    EXPECT_TRUE(typed_config.lb_config_.has_locality_lb_config());
    EXPECT_TRUE(typed_config.lb_config_.locality_lb_config().has_locality_weighted_lb_config());
    EXPECT_FALSE(typed_config.lb_config_.locality_lb_config().has_zone_aware_lb_config());
  }

  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig common;

    common.mutable_zone_aware_lb_config()->mutable_min_cluster_size()->set_value(3);
    common.mutable_zone_aware_lb_config()->mutable_routing_enabled()->set_value(23.0);
    common.mutable_zone_aware_lb_config()->set_fail_traffic_on_panic(true);

    Extensions::LoadBalancingPolices::Random::TypedRandomLbConfig typed_config(common);

    EXPECT_TRUE(typed_config.lb_config_.has_locality_lb_config());
    EXPECT_FALSE(typed_config.lb_config_.locality_lb_config().has_locality_weighted_lb_config());
    EXPECT_TRUE(typed_config.lb_config_.locality_lb_config().has_zone_aware_lb_config());

    const auto& zone_aware_lb_config =
        typed_config.lb_config_.locality_lb_config().zone_aware_lb_config();
    EXPECT_EQ(zone_aware_lb_config.min_cluster_size().value(), 3);
    EXPECT_DOUBLE_EQ(zone_aware_lb_config.routing_enabled().value(), 23.0);
    EXPECT_TRUE(zone_aware_lb_config.fail_traffic_on_panic());
  }
}

} // namespace
} // namespace Upstream
} // namespace Envoy
