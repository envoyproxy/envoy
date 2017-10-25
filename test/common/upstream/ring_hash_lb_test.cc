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
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  RingHashLoadBalancer lb_{cluster_, stats_, runtime_, random_};
};

TEST_F(RingHashLoadBalancerTest, NoHost) { EXPECT_EQ(nullptr, lb_.chooseHost(nullptr)); };

TEST_F(RingHashLoadBalancerTest, Basic) {
  cluster_.hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:81"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:82"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:83"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:84"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:85")};
  cluster_.healthy_hosts_ = cluster_.hosts_;

  ON_CALL(runtime_.snapshot_, getInteger("upstream.ring_hash.min_ring_size", _))
      .WillByDefault(Return(12));
  cluster_.runCallbacks({}, {});

  // This is the hash ring built using the default hash (probably murmur2) on GCC 5.4.
  // TODO(mattklein123): Compile in and use murmur3 or city so we know exactly
  // what we are going to get.
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
    EXPECT_EQ(cluster_.hosts_[5], lb_.chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(cluster_.hosts_[5], lb_.chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(1358027074129602068);
    EXPECT_EQ(cluster_.hosts_[5], lb_.chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(1358027074129602069);
    EXPECT_EQ(cluster_.hosts_[3], lb_.chooseHost(&context));
  }
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(10150910876324007730UL));
    EXPECT_EQ(cluster_.hosts_[2], lb_.chooseHost(nullptr));
  }
  EXPECT_EQ(0UL, stats_.lb_healthy_panic_.value());

  cluster_.healthy_hosts_.clear();
  cluster_.runCallbacks({}, {});
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(cluster_.hosts_[5], lb_.chooseHost(&context));
  }
  EXPECT_EQ(1UL, stats_.lb_healthy_panic_.value());
}

TEST_F(RingHashLoadBalancerTest, UnevenHosts) {
  cluster_.hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:81")};
  ON_CALL(runtime_.snapshot_, getInteger("upstream.ring_hash.min_ring_size", _))
      .WillByDefault(Return(3));
  cluster_.runCallbacks({}, {});

  // This is the hash ring built using the default hash (probably murmur2) on GCC 5.4.
  // TODO(mattklein123): Compile in and use murmur3 or city so we know exactly
  // what we are going to get.
  // ring hash: host=127.0.0.1:81 hash=7701421856454313576
  // ring hash: host=127.0.0.1:81 hash=9887544217113020895
  // ring hash: host=127.0.0.1:80 hash=15427156902705414897
  // ring hash: host=127.0.0.1:80 hash=17613279263364193813
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(cluster_.hosts_[1], lb_.chooseHost(&context));
  }

  cluster_.hosts_ = {makeTestHost(cluster_.info_, "tcp://127.0.0.1:81"),
                     makeTestHost(cluster_.info_, "tcp://127.0.0.1:82")};
  cluster_.runCallbacks({}, {});

  // This is the hash ring built using the default hash (probably murmur2) on GCC 5.4.
  // TODO(mattklein123): Compile in and use murmur3 or city so we know exactly
  // what we are going to get.
  // ring hash: host=127.0.0.1:81 hash=7701421856454313576
  // ring hash: host=127.0.0.1:82 hash=8649315368077433379
  // ring hash: host=127.0.0.1:81 hash=9887544217113020895
  // ring hash: host=127.0.0.1:82 hash=10150910876324007731
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(cluster_.hosts_[0], lb_.chooseHost(&context));
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

  ON_CALL(runtime_.snapshot_, getInteger("upstream.ring_hash.min_ring_size", _))
      .WillByDefault(Return(min_ring_size));
  cluster_.runCallbacks({}, {});

  for (uint64_t i = 0; i < keys_to_simulate; i++) {
    TestLoadBalancerContext context(std::hash<std::string>()(fmt::format("{}", i)));
    hit_counter[lb_.chooseHost(&context)->address()->asString()] += 1;
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
