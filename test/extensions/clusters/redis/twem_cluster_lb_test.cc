#include <memory>

#include "source/extensions/clusters/redis/twem_cluster_lb.h"

#include "extensions/filters/network/common/redis/client.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/simulated_time_system.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace Redis {

class TwemLoadBalancerRingTest : public testing::Test {
public:
  TwemLoadBalancerRingTest() : scope_(stats_store_.createScope("")) {}

  void init() {
    // TODO (zhangyinghao) modify state;
    Upstream::RingHashLoadBalancerStats state = {ALL_RING_HASH_LOAD_BALANCER_STATS(POOL_GAUGE(*scope_))};
    ring_ = std::make_unique<TwemClusterThreadAwareLoadBalancer::Ring>(normalized_host_weights_, min_normalized_weight_, ring_size_, ring_size_,
      hash_function_, use_hostname_for_hashing_, state);
  }

  std::unique_ptr<TwemClusterThreadAwareLoadBalancer::Ring> ring_;

  Upstream::NormalizedHostWeightVector normalized_host_weights_;
  double min_normalized_weight_;
  bool use_hostname_for_hashing_;
  uint64_t ring_size_;
  envoy::config::cluster::v3::Cluster::RingHashLbConfig::HashFunction hash_function_;
  Stats::IsolatedStoreImpl stats_store_;
  Stats::ScopePtr scope_;
};

TEST_F(TwemLoadBalancerRingTest, doHash) {
  init();

  std::vector<uint64_t> hashes;
  ring_->doHash("foo", hash_function_, 0, hashes);
  ring_->doHash("bar", hash_function_, 0, hashes);

  EXPECT_EQ(static_cast<size_t>(8), hashes.size());
  EXPECT_EQ(hashes[0], 2128116278); // key: foo-0 align: 0
  EXPECT_EQ(hashes[1], 2956815523); // key: foo-0 align: 1
  EXPECT_EQ(hashes[2], 2636934948); // key: foo-0 align: 2
  EXPECT_EQ(hashes[3], 2090084729); // key: foo-0 align: 3

  EXPECT_EQ(hashes[4], 2873245867); // key: bar-0 align: 0
  EXPECT_EQ(hashes[5], 2320091756); // key: bar-0 align: 1
  EXPECT_EQ(hashes[6], 243337714);  // key: bar-0 align: 2
  EXPECT_EQ(hashes[7], 1067106703); // key: bar-0 align: 3
}

class TestLoadBalancerContext : public Upstream::LoadBalancerContextBase {
public:
  TestLoadBalancerContext(uint64_t hash_key) : hash_key_(hash_key) {}

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return hash_key_; }

  absl::optional<uint64_t> hash_key_;
};

class TwemClusterLoadBalancerTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  TwemClusterLoadBalancerTest() : stat_names_(stats_store_.symbolTable()),
                                  stats_(Upstream::ClusterInfoImpl::generateStats(stats_store_, stat_names_)) {}

  void init() {
    lb_ = std::make_unique<TwemClusterThreadAwareLoadBalancer>(priority_set_, stats_, stats_store_,
                                runtime_, random_, config_, common_config_);
    lb_->initialize();
  }

  // Run all tests against both priority 0 and priority 1 host sets, to ensure
  // all the load balancers have equivalent functionality for failover host sets.
  Upstream::MockHostSet& hostSet() { return host_set_; }

  std::unique_ptr<TwemClusterThreadAwareLoadBalancer> lb_;
  std::shared_ptr<Upstream::MockClusterInfo> info_{new NiceMock<Upstream::MockClusterInfo>()};
  absl::optional<envoy::config::cluster::v3::Cluster::RingHashLbConfig> config_;
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  envoy::config::cluster::v3::Cluster::RingHashLbConfig::HashFunction null_hash_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  Stats::IsolatedStoreImpl stats_store_;
  Upstream::ClusterStatNames stat_names_;
  Upstream::ClusterStats stats_;
  Upstream::MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
};

// empty ring hash
TEST_F(TwemClusterLoadBalancerTest, NoHost) {
  init();
  EXPECT_EQ(nullptr, lb_->factory()->create()->chooseHost(nullptr));
}

// twem ring hash
TEST_F(TwemClusterLoadBalancerTest, Ring) {
  hostSet().hosts_ = {Upstream::makeTestHost(info_, "foo", "tcp://127.0.0.1:90", simTime()),
                      Upstream::makeTestHost(info_, "bar", "tcp://127.0.0.1:91", simTime())};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  common_config_ = envoy::config::cluster::v3::Cluster::CommonLbConfig();
  auto chc = envoy::config::cluster::v3::Cluster::CommonLbConfig::ConsistentHashingLbConfig();
  chc.set_use_hostname_for_hashing(true);
  common_config_.set_allocated_consistent_hashing_lb_config(&chc);

  init();

  // This is the hash ring built using twem hash.
  // ring hash: hostname = foo  hash = 59744702
  // ring hash: hostname = foo  hash = 61711414
  // ring hash: hostname = foo  hash = 62132387
  // ring hash: hostname = foo  hash = 65502830
  // ring hash: hostname = foo  hash = 83963956
  // ring hash: hostname = foo  hash = 96297134
  // ring hash: hostname = bar  hash = 103518541
  // ring hash: hostname = bar  hash = 114677069
  // ...
  // ring hash: hostname = bar  hash = 4184128594
  // ring hash: hostname = foo  hash = 4206269070
  // ring hash: hostname = bar  hash = 4210924796
  // ring hash: hostname = bar  hash = 4242669331
  // ring hash: hostname = bar  hash = 4253234599
  // ring hash: hostname = bar  hash = 4257721138
  // ring hash: hostname = foo  hash = 4269064192
  // ring hash: hostname = foo  hash = 4280638983
  // ring hash: hostname = bar  hash = 4285783743
  // ring hash: hostname = bar  hash = 4289734534

  Upstream::LoadBalancerPtr lb = lb_->factory()->create();

  {
    TestLoadBalancerContext context(0);
    EXPECT_EQ(hostSet().hosts_[0], lb->chooseHost(&context));
  }

  {
    TestLoadBalancerContext context(std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(hostSet().hosts_[0], lb->chooseHost(&context));
  }

  {
    TestLoadBalancerContext context(65502829);
    EXPECT_EQ(hostSet().hosts_[0], lb->chooseHost(&context));
  }

  {
    TestLoadBalancerContext context(103518540);
    EXPECT_EQ(hostSet().hosts_[1], lb->chooseHost(&context));
  }
}

class TwemLoadBalancerContextImplTest : public testing::Test {};

TEST_F(TwemLoadBalancerContextImplTest, Basic) {
  TwemLoadBalancerContextImpl ctx1("foo");
  EXPECT_EQ(absl::optional<uint64_t>(4275688823), ctx1.computeHashKey());

  TwemLoadBalancerContextImpl ctx2("bar");
  EXPECT_EQ(absl::optional<uint64_t>(322520602), ctx2.computeHashKey());

  TwemLoadBalancerContextImpl ctx3("bilibili");
  EXPECT_EQ(absl::optional<uint64_t>(4156757469), ctx3.computeHashKey());
}

} // namespace Redis
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy

