#include <cstdint>
#include <string>

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

class RingHashLoadBalancerTest : public testing::Test {
public:
  RingHashLoadBalancerTest() : stats_(ClusterInfoImpl::generateStats(stats_store_)) {}

  NiceMock<MockCluster> cluster_;
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  Optional<envoy::api::v2::Cluster::RingHashLbConfig> config_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
};

TEST_F(RingHashLoadBalancerTest, NoHost) {
  RingHashLoadBalancer lb{cluster_, stats_, runtime_, random_, config_};
  EXPECT_EQ(nullptr, lb.chooseHost(nullptr));
};

TEST_F(RingHashLoadBalancerTest, Basic) {
  cluster_.hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:90"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:91"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:92"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:93"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:94"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:95")};
  cluster_.healthy_hosts_ = cluster_.hosts_;
  cluster_.runCallbacks({}, {});

  config_.value(envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(12);
  config_.value().mutable_deprecated_v1()->mutable_use_std_hash()->set_value(false);

  RingHashLoadBalancer lb{cluster_, stats_, runtime_, random_, config_};

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

  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(cluster_.hosts_[4], lb.chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(cluster_.hosts_[4], lb.chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(3551244743356806947);
    EXPECT_EQ(cluster_.hosts_[5], lb.chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(3551244743356806948);
    EXPECT_EQ(cluster_.hosts_[3], lb.chooseHost(&context));
  }
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(16117243373044804880UL));
    EXPECT_EQ(cluster_.hosts_[0], lb.chooseHost(nullptr));
  }
  EXPECT_EQ(0UL, stats_.lb_healthy_panic_.value());

  cluster_.healthy_hosts_.clear();
  cluster_.runCallbacks({}, {});
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(cluster_.hosts_[4], lb.chooseHost(&context));
  }
  EXPECT_EQ(1UL, stats_.lb_healthy_panic_.value());
}

#ifndef __APPLE__
// Run similar tests with the default hash algorithm for GCC 5.
// TODO(danielhochman): After v1 is deprecated this test can be deleted since std::hash will no
// longer be in use.
TEST_F(RingHashLoadBalancerTest, BasicWithStdHash) {
  cluster_.hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:81"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:82"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:83"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:84"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:85")};
  cluster_.healthy_hosts_ = cluster_.hosts_;
  cluster_.runCallbacks({}, {});

  // use_std_hash defaults to true so don't set it here.
  config_.value(envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(12);
  RingHashLoadBalancer lb{cluster_, stats_, runtime_, random_, config_};

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
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(cluster_.hosts_[5], lb.chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(cluster_.hosts_[5], lb.chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(1358027074129602068);
    EXPECT_EQ(cluster_.hosts_[5], lb.chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(1358027074129602069);
    EXPECT_EQ(cluster_.hosts_[3], lb.chooseHost(&context));
  }
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(10150910876324007730UL));
    EXPECT_EQ(cluster_.hosts_[2], lb.chooseHost(nullptr));
  }
  EXPECT_EQ(0UL, stats_.lb_healthy_panic_.value());
}
#endif

TEST_F(RingHashLoadBalancerTest, UnevenHosts) {
  cluster_.hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")};
  cluster_.runCallbacks({}, {});

  config_.value(envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(3);
  config_.value().mutable_deprecated_v1()->mutable_use_std_hash()->set_value(false);
  RingHashLoadBalancer lb{cluster_, stats_, runtime_, random_, config_};

  // hash ring:
  // port | position
  // ---------------------------
  // :80  | 5454692015285649509
  // :81  | 7859399908942313493
  // :80  | 13838424394637650569
  // :81  | 16064866803292627174

  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(cluster_.hosts_[0], lb.chooseHost(&context));
  }

  cluster_.hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:81"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:82")};
  cluster_.runCallbacks({}, {});

  // hash ring:
  // port | position
  // ------------------
  // :81  | 7859399908942313493
  // :82  | 8241336090459785962
  // :82  | 12882406409176325258
  // :81  | 16064866803292627174

  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(cluster_.hosts_[0], lb.chooseHost(&context));
  }
}

/**
 * This test is for simulation only and should not be run as part of unit tests. In order to run the
 * simulation remove the DISABLED_ prefix from the TEST_F invocation. Run bazel with
 * "--test_output all" to see the output.
 *
 * The test is designed to output hit rate and percentage of total hits per-host for different
 * minimum ring sizes. Most testing should be done with a rough order of magnitude number of hosts
 * as in a production environment, and testing different orders of magnitude for the min_ring_size.
 *
 * The output looks like:
 *
 * hits      hit%   server
 * ===============================
 * 60000     60.00  10.0.0.1:22120
 * 40000     40.00  10.0.0.2:22120
 *
 * Larger ring sizes will result in better distribution, but come at the cost of more memory,
 * more time to build the ring, and slightly slower lookup when determining the backend for a
 * key.
 */
class DISABLED_RingHashLoadBalancerTest : public RingHashLoadBalancerTest {
public:
  DISABLED_RingHashLoadBalancerTest() : RingHashLoadBalancerTest(){};
};

TEST_F(DISABLED_RingHashLoadBalancerTest, DetermineSpread) {
  const uint64_t num_hosts = 100;
  const uint64_t keys_to_simulate = 10000;
  const uint64_t min_ring_size = 65536;
  std::unordered_map<std::string, uint64_t> hit_counter;

  // TODO(danielhochman): add support for more hosts if necessary with another loop for subnet
  ASSERT_LT(num_hosts, 256);
  for (uint64_t i = 0; i < num_hosts; i++) {
    cluster_.hosts_.push_back(makeTestHost(cluster_.info_, fmt::format("tcp://10.0.0.{}:6379", i)));
  }
  cluster_.healthy_hosts_ = cluster_.hosts_;
  cluster_.runCallbacks({}, {});

  config_.value(envoy::api::v2::Cluster::RingHashLbConfig());
  config_.value().mutable_minimum_ring_size()->set_value(min_ring_size);
  config_.value().mutable_deprecated_v1()->mutable_use_std_hash()->set_value(false);
  RingHashLoadBalancer lb{cluster_, stats_, runtime_, random_, config_};

  for (uint64_t i = 0; i < keys_to_simulate; i++) {
    TestLoadBalancerContext context(std::hash<std::string>()(fmt::format("{}", i)));
    hit_counter[lb.chooseHost(&context)->address()->asString()] += 1;
  }

  std::cout << fmt::format("{:<9}  {:<4}  {:<20}", "hits", "%hit", "server") << std::endl;
  std::cout << "===============================" << std::endl;
  for (auto it = hit_counter.begin(); it != hit_counter.end(); it++) {
    std::cout << fmt::format("{:<9}  {:03.2f}  {:<20}", it->second,
                             100 * static_cast<double>(it->second) / keys_to_simulate, it->first)
              << std::endl;
  }
}

} // namespace Upstream
} // namespace Envoy
