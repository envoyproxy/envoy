#include <cstdint>
#include <string>
#include <unordered_map>

#include "envoy/router/router.h"

#include "common/network/utility.h"
#include "common/upstream/ring_hash_lb.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Upstream {

class TestLoadBalancerContext : public LoadBalancerContext {
public:
  TestLoadBalancerContext(uint64_t hash_key) : hash_key_(hash_key) {}

  // Upstream::LoadBalancerContext
  Optional<uint64_t> computeHashKey() override { return hash_key_; }
  const Router::MetadataMatchCriteria* metadataMatchCriteria() const override { return nullptr; }
  const Network::Connection* downstreamConnection() const override { return nullptr; }

  Optional<uint64_t> hash_key_;
};

class RingHashLoadBalancerTest : public ::testing::TestWithParam<bool> {
public:
  RingHashLoadBalancerTest() : stats_(ClusterInfoImpl::generateStats(stats_store_)) {}

  void init() {
    lb_.reset(new RingHashLoadBalancer(priority_set_, stats_, runtime_, random_, config_,
                                       common_config_));
    lb_->initialize();
  }

  // Run all tests aginst both priority 0 and priority 1 host sets, to ensure
  // all the load balancers have equivalent functonality for failover host sets.
  MockHostSet& hostSet() { return GetParam() ? host_set_ : failover_host_set_; }

  NiceMock<MockPrioritySet> priority_set_;
  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  MockHostSet& failover_host_set_ = *priority_set_.getMockHostSet(1);
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  Optional<envoy::api::v2::Cluster::RingHashLbConfig> config_;
  envoy::api::v2::Cluster::CommonLbConfig common_config_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::unique_ptr<RingHashLoadBalancer> lb_;
};

// For tests which don't need to be run in both primary and failover modes.
typedef RingHashLoadBalancerTest RingHashFailoverTest;

INSTANTIATE_TEST_CASE_P(RingHashPrimaryOrFailover, RingHashLoadBalancerTest,
                        ::testing::Values(true, false));
INSTANTIATE_TEST_CASE_P(RingHashPrimaryOrFailover, RingHashFailoverTest, ::testing::Values(true));

TEST_P(RingHashLoadBalancerTest, NoHost) {
  init();
  EXPECT_EQ(nullptr, lb_->factory()->create()->chooseHost(nullptr));
};

TEST_P(RingHashLoadBalancerTest, Basic) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93"),
      makeTestHost(info_, "tcp://127.0.0.1:94"), makeTestHost(info_, "tcp://127.0.0.1:95")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_.value(envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(12);
  config_.value().mutable_deprecated_v1()->mutable_use_std_hash()->set_value(false);

  init();

  // hash ring:
  // port | position
  // ---------------------------
  // :94  | 833437586790550860
  // :92  | 928266305478181108
  // :90  | 1033482794131418490
  // :95  | 3551244743356806947
  // :93  | 3851675632748031481
  // :91  | 5583722120771150861
  // :91  | 6311230543546372928
  // :93  | 7700377290971790572
  // :95  | 13144177310400110813
  // :92  | 13444792449719432967
  // :94  | 15516499411664133160
  // :90  | 16117243373044804889

  LoadBalancerPtr lb = lb_->factory()->create();
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(hostSet().hosts_[4], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(hostSet().hosts_[4], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(3551244743356806947);
    EXPECT_EQ(hostSet().hosts_[5], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(3551244743356806948);
    EXPECT_EQ(hostSet().hosts_[3], lb->chooseHost(&context));
  }
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(16117243373044804880UL));
    EXPECT_EQ(hostSet().hosts_[0], lb->chooseHost(nullptr));
  }
  EXPECT_EQ(0UL, stats_.lb_healthy_panic_.value());

  hostSet().healthy_hosts_.clear();
  hostSet().runCallbacks({}, {});
  lb = lb_->factory()->create();
  {
    TestLoadBalancerContext context(0);
    if (GetParam() == 1) {
      EXPECT_EQ(hostSet().hosts_[4], lb->chooseHost(&context));
    } else {
      // When all hosts are unhealthy, the default behavior of the load balancer is to send
      // traffic to P=0. In this case, P=0 has no backends so it returns nullptr.
      EXPECT_EQ(nullptr, lb->chooseHost(&context));
    }
  }
  EXPECT_EQ(1UL, stats_.lb_healthy_panic_.value());
}

// Ensure if all the hosts with priority 0 unhealthy, the next priority hosts are used.
TEST_P(RingHashFailoverTest, BasicFailover) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80")};
  failover_host_set_.healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:82")};
  failover_host_set_.hosts_ = failover_host_set_.healthy_hosts_;

  config_.value(envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(12);
  config_.value().mutable_deprecated_v1()->mutable_use_std_hash()->set_value(false);
  init();

  LoadBalancerPtr lb = lb_->factory()->create();
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb->chooseHost(nullptr));

  // Add a healthy host at P=0 and it will be chosen.
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks({}, {});
  lb = lb_->factory()->create();
  EXPECT_EQ(host_set_.healthy_hosts_[0], lb->chooseHost(nullptr));

  // Remove the healthy host and ensure we fail back over to the failover_host_set_
  host_set_.healthy_hosts_ = {};
  host_set_.runCallbacks({}, {});
  lb = lb_->factory()->create();
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb->chooseHost(nullptr));

  // Set up so P=0 gets 70% of the load, and P=1 gets 30%.
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80"),
                      makeTestHost(info_, "tcp://127.0.0.1:81")};
  host_set_.healthy_hosts_ = {host_set_.hosts_[0]};
  host_set_.runCallbacks({}, {});
  lb = lb_->factory()->create();
  EXPECT_CALL(random_, random()).WillOnce(Return(69));
  EXPECT_EQ(host_set_.healthy_hosts_[0], lb->chooseHost(nullptr));
  EXPECT_CALL(random_, random()).WillOnce(Return(71));
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb->chooseHost(nullptr));
}

#ifndef __APPLE__
// Run similar tests with the default hash algorithm for GCC 5.
// TODO(danielhochman): After v1 is deprecated this test can be deleted since std::hash will no
// longer be in use.
TEST_P(RingHashLoadBalancerTest, BasicWithStdHash) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:80"), makeTestHost(info_, "tcp://127.0.0.1:81"),
      makeTestHost(info_, "tcp://127.0.0.1:82"), makeTestHost(info_, "tcp://127.0.0.1:83"),
      makeTestHost(info_, "tcp://127.0.0.1:84"), makeTestHost(info_, "tcp://127.0.0.1:85")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  // use_std_hash defaults to true so don't set it here.
  config_.value(envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(12);
  init();

  // This is the hash ring built using the default hash (probably murmur2) on GCC 5.4.
  // ring hash: host=127.0.0.1:85 hash=1358027074129602068
  // ring hash: host=127.0.0.1:83 hash=4361834613929391114
  // ring hash: host=127.0.0.1:84 hash=7224494972555149682
  // ring hash: host=127.0.0.1:81 hash=7701421856454313576
  // ring hash: host=127.0.0.1:82 hash=8649315368077433379
  // ring hash: host=127.0.0.1:84 hash=8739448859063030639
  // ring hash: host=127.0.0.1:81 hash=9887544217113020895
  // ring hash: host=127.0.0.1:82 hash=10150910876324007731
  // ring hash: host=127.0.0.1:83 hash=15168472011420622455
  // ring hash: host=127.0.0.1:80 hash=15427156902705414897
  // ring hash: host=127.0.0.1:85 hash=16375050414328759093
  // ring hash: host=127.0.0.1:80 hash=17613279263364193813
  LoadBalancerPtr lb = lb_->factory()->create();
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(hostSet().hosts_[5], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(hostSet().hosts_[5], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(1358027074129602068);
    EXPECT_EQ(hostSet().hosts_[5], lb->chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(1358027074129602069);
    EXPECT_EQ(hostSet().hosts_[3], lb->chooseHost(&context));
  }
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(10150910876324007730UL));
    EXPECT_EQ(hostSet().hosts_[2], lb->chooseHost(nullptr));
  }
  EXPECT_EQ(0UL, stats_.lb_healthy_panic_.value());
}
#endif

TEST_P(RingHashLoadBalancerTest, UnevenHosts) {
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80"),
                      makeTestHost(info_, "tcp://127.0.0.1:81")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_.value(envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(3);
  config_.value().mutable_deprecated_v1()->mutable_use_std_hash()->set_value(false);
  init();

  // hash ring:
  // port | position
  // ---------------------------
  // :80  | 5454692015285649509
  // :81  | 7859399908942313493
  // :80  | 13838424394637650569
  // :81  | 16064866803292627174

  LoadBalancerPtr lb = lb_->factory()->create();
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(hostSet().hosts_[0], lb->chooseHost(&context));
  }

  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:81"),
                      makeTestHost(info_, "tcp://127.0.0.1:82")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  // hash ring:
  // port | position
  // ------------------
  // :81  | 7859399908942313493
  // :82  | 8241336090459785962
  // :82  | 12882406409176325258
  // :81  | 16064866803292627174

  lb = lb_->factory()->create();
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(hostSet().hosts_[0], lb->chooseHost(&context));
  }
}

} // namespace Upstream
} // namespace Envoy
