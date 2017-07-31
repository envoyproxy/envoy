#include <cstdint>
#include <string>

#include "common/network/utility.h"
#include "common/upstream/ring_hash_lb.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Upstream {

static HostSharedPtr newTestHost(Upstream::ClusterInfoConstSharedPtr cluster,
                                 const std::string& url) {
  return std::make_shared<HostImpl>(cluster, "", Network::Utility::resolveUrl(url), false, 1, "");
}

class TestLoadBalancerContext : public LoadBalancerContext {
public:
  TestLoadBalancerContext(uint64_t hash_key) : hash_key_(hash_key) {}

  // Upstream::LoadBalancerContext
  Optional<uint64_t> hashKey() const override { return hash_key_; }

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
  cluster_.hosts_ = {newTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                     newTestHost(cluster_.info_, "tcp://127.0.0.1:81"),
                     newTestHost(cluster_.info_, "tcp://127.0.0.1:82"),
                     newTestHost(cluster_.info_, "tcp://127.0.0.1:83"),
                     newTestHost(cluster_.info_, "tcp://127.0.0.1:84"),
                     newTestHost(cluster_.info_, "tcp://127.0.0.1:85")};
  cluster_.healthy_hosts_ = cluster_.hosts_;

  ON_CALL(runtime_.snapshot_, getInteger("upstream.ring_hash.min_ring_size", _))
      .WillByDefault(Return(12));
  cluster_.runCallbacks({}, {});

#if !defined(__APPLE__)
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

  cluster_.healthy_hosts_.clear();
  cluster_.runCallbacks({}, {});
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(cluster_.hosts_[5], lb_.chooseHost(&context));
  }
#else
  // Similarly, this is what you get on OSX/Apple LLVM 8.1.0.
  // ring hash: hash_key=127.0.0.1:80_0 hash=1785278789362412239
  // ring hash: hash_key=127.0.0.1:85_0 hash=4449229172670576906
  // ring hash: hash_key=127.0.0.1:83_1 hash=6055030134139063662
  // ring hash: hash_key=127.0.0.1:81_1 hash=6131885104641997928
  // ring hash: hash_key=127.0.0.1:84_0 hash=6367537078542552970
  // ring hash: hash_key=127.0.0.1:81_0 hash=8908282605774288966
  // ring hash: hash_key=127.0.0.1:83_0 hash=11126513076073360795
  // ring hash: hash_key=127.0.0.1:80_1 hash=11937214931531445650
  // ring hash: hash_key=127.0.0.1:82_0 hash=12081020427914561087
  // ring hash: hash_key=127.0.0.1:84_1 hash=12813936884733316938
  // ring hash: hash_key=127.0.0.1:85_1 hash=14420433723247909236
  // ring hash: hash_key=127.0.0.1:82_1 hash=17574187438183464265
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(cluster_.hosts_[0], lb_.chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(cluster_.hosts_[0], lb_.chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(8908282605774288966);
    EXPECT_EQ(cluster_.hosts_[1], lb_.chooseHost(&context));
  }
  {
    TestLoadBalancerContext context(8908282605774288967);
    EXPECT_EQ(cluster_.hosts_[3], lb_.chooseHost(&context));
  }
  {
    EXPECT_CALL(random_, random()).WillOnce(Return(12081020427914561087UL));
    EXPECT_EQ(cluster_.hosts_[2], lb_.chooseHost(nullptr));
  }

  cluster_.healthy_hosts_.clear();
  cluster_.runCallbacks({}, {});
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(cluster_.hosts_[0], lb_.chooseHost(&context));
  }
#endif
}

TEST_F(RingHashLoadBalancerTest, UnevenHosts) {
  cluster_.hosts_ = {newTestHost(cluster_.info_, "tcp://127.0.0.1:80"),
                     newTestHost(cluster_.info_, "tcp://127.0.0.1:81")};
  ON_CALL(runtime_.snapshot_, getInteger("upstream.ring_hash.min_ring_size", _))
      .WillByDefault(Return(3));
  cluster_.runCallbacks({}, {});

#if !defined(__APPLE__)
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
#else
  // Similarly, this is what you get on OSX/Apple LLVM 8.1.0.
  // ring hash: hash_key=127.0.0.1:80_0 hash=1785278789362412239
  // ring hash: hash_key=127.0.0.1:80_1 hash=11937214931531445650
  // ring hash: hash_key=127.0.0.1:81_0 hash=8908282605774288966
  // ring hash: hash_key=127.0.0.1:81_1 hash=6131885104641997928
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(cluster_.hosts_[0], lb_.chooseHost(&context));
  }
#endif

  cluster_.hosts_ = {newTestHost(cluster_.info_, "tcp://127.0.0.1:81"),
                     newTestHost(cluster_.info_, "tcp://127.0.0.1:82")};
  cluster_.runCallbacks({}, {});
#if !defined(__APPLE__)
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
#else
  // Similarly, this is what you get on OSX/Apple LLVM 8.1.0.
  // ring hash: hash_key=127.0.0.1:81_0 hash=8908282605774288966
  // ring hash: hash_key=127.0.0.1:81_1 hash=6131885104641997928
  // ring hash: hash_key=127.0.0.1:82_0 hash=12081020427914561087
  // ring hash: hash_key=127.0.0.1:82_1 hash=17574187438183464265
  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(cluster_.hosts_[0], lb_.chooseHost(&context));
  }
#endif
}

} // namespace Upstream
} // namespace Envoy
