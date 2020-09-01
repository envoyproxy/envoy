#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "common/upstream/maglev_lb.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/priority_set.h"

namespace Envoy {
namespace Upstream {
namespace {

class TestLoadBalancerContext : public LoadBalancerContextBase {
public:
  using HostPredicate = std::function<bool(const Host&)>;

  TestLoadBalancerContext(uint64_t hash_key)
      : TestLoadBalancerContext(hash_key, 0, [](const Host&) { return false; }) {}
  TestLoadBalancerContext(uint64_t hash_key, uint32_t retry_count,
                          HostPredicate should_select_another_host)
      : hash_key_(hash_key), retry_count_(retry_count),
        should_select_another_host_(should_select_another_host) {}

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return hash_key_; }
  uint32_t hostSelectionRetryCount() const override { return retry_count_; };
  bool shouldSelectAnotherHost(const Host& host) override {
    return should_select_another_host_(host);
  }

  absl::optional<uint64_t> hash_key_;
  uint32_t retry_count_;
  HostPredicate should_select_another_host_;
};

// Note: ThreadAwareLoadBalancer base is heavily tested by RingHashLoadBalancerTest. Only basic
//       functionality is covered here.
class MaglevLoadBalancerTest : public testing::Test {
public:
  MaglevLoadBalancerTest() : stats_(ClusterInfoImpl::generateStats(stats_store_)) {}

  void init(uint32_t table_size) {
    lb_ = std::make_unique<MaglevLoadBalancer>(priority_set_, stats_, stats_store_, runtime_,
                                               random_, common_config_, table_size);
    lb_->initialize();
  }

  NiceMock<MockPrioritySet> priority_set_;
  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  std::unique_ptr<MaglevLoadBalancer> lb_;
};

// Works correctly without any hosts.
TEST_F(MaglevLoadBalancerTest, NoHost) {
  init(7);
  EXPECT_EQ(nullptr, lb_->factory()->create()->chooseHost(nullptr));
};

// Basic sanity tests.
TEST_F(MaglevLoadBalancerTest, Basic) {
  host_set_.hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93"),
      makeTestHost(info_, "tcp://127.0.0.1:94"), makeTestHost(info_, "tcp://127.0.0.1:95")};
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks({}, {});
  init(7);

  EXPECT_EQ("maglev_lb.min_entries_per_host", lb_->stats().min_entries_per_host_.name());
  EXPECT_EQ("maglev_lb.max_entries_per_host", lb_->stats().max_entries_per_host_.name());
  EXPECT_EQ(1, lb_->stats().min_entries_per_host_.value());
  EXPECT_EQ(2, lb_->stats().max_entries_per_host_.value());

  // maglev: i=0 host=127.0.0.1:92
  // maglev: i=1 host=127.0.0.1:94
  // maglev: i=2 host=127.0.0.1:90
  // maglev: i=3 host=127.0.0.1:91
  // maglev: i=4 host=127.0.0.1:95
  // maglev: i=5 host=127.0.0.1:90
  // maglev: i=6 host=127.0.0.1:93
  LoadBalancerPtr lb = lb_->factory()->create();
  const std::vector<uint32_t> expected_assignments{2, 4, 0, 1, 5, 0, 3};
  for (uint32_t i = 0; i < 3 * expected_assignments.size(); ++i) {
    TestLoadBalancerContext context(i);
    EXPECT_EQ(host_set_.hosts_[expected_assignments[i % expected_assignments.size()]],
              lb->chooseHost(&context));
  }
}

// Basic with hostname.
TEST_F(MaglevLoadBalancerTest, BasicWithHostName) {
  host_set_.hosts_ = {makeTestHost(info_, "90", "tcp://127.0.0.1:90"),
                      makeTestHost(info_, "91", "tcp://127.0.0.1:91"),
                      makeTestHost(info_, "92", "tcp://127.0.0.1:92"),
                      makeTestHost(info_, "93", "tcp://127.0.0.1:93"),
                      makeTestHost(info_, "94", "tcp://127.0.0.1:94"),
                      makeTestHost(info_, "95", "tcp://127.0.0.1:95")};
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks({}, {});
  common_config_ = envoy::config::cluster::v3::Cluster::CommonLbConfig();
  auto chc = envoy::config::cluster::v3::Cluster::CommonLbConfig::ConsistentHashingLbConfig();
  chc.set_use_hostname_for_hashing(true);
  common_config_.set_allocated_consistent_hashing_lb_config(&chc);
  init(7);
  common_config_.release_consistent_hashing_lb_config();

  EXPECT_EQ("maglev_lb.min_entries_per_host", lb_->stats().min_entries_per_host_.name());
  EXPECT_EQ("maglev_lb.max_entries_per_host", lb_->stats().max_entries_per_host_.name());
  EXPECT_EQ(1, lb_->stats().min_entries_per_host_.value());
  EXPECT_EQ(2, lb_->stats().max_entries_per_host_.value());

  // maglev: i=0 host=92
  // maglev: i=1 host=95
  // maglev: i=2 host=90
  // maglev: i=3 host=93
  // maglev: i=4 host=94
  // maglev: i=5 host=91
  // maglev: i=6 host=90
  LoadBalancerPtr lb = lb_->factory()->create();
  const std::vector<uint32_t> expected_assignments{2, 5, 0, 3, 4, 1, 0};
  for (uint32_t i = 0; i < 3 * expected_assignments.size(); ++i) {
    TestLoadBalancerContext context(i);
    EXPECT_EQ(host_set_.hosts_[expected_assignments[i % expected_assignments.size()]],
              lb->chooseHost(&context));
  }
}

// Same ring as the Basic test, but exercise retry host predicate behavior.
TEST_F(MaglevLoadBalancerTest, BasicWithRetryHostPredicate) {
  host_set_.hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93"),
      makeTestHost(info_, "tcp://127.0.0.1:94"), makeTestHost(info_, "tcp://127.0.0.1:95")};
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks({}, {});
  init(7);

  EXPECT_EQ("maglev_lb.min_entries_per_host", lb_->stats().min_entries_per_host_.name());
  EXPECT_EQ("maglev_lb.max_entries_per_host", lb_->stats().max_entries_per_host_.name());
  EXPECT_EQ(1, lb_->stats().min_entries_per_host_.value());
  EXPECT_EQ(2, lb_->stats().max_entries_per_host_.value());

  // maglev: i=0 host=127.0.0.1:92
  // maglev: i=1 host=127.0.0.1:94
  // maglev: i=2 host=127.0.0.1:90
  // maglev: i=3 host=127.0.0.1:91
  // maglev: i=4 host=127.0.0.1:95
  // maglev: i=5 host=127.0.0.1:90
  // maglev: i=6 host=127.0.0.1:93
  LoadBalancerPtr lb = lb_->factory()->create();
  {
    // Confirm that i=3 is selected by the hash.
    TestLoadBalancerContext context(10);
    EXPECT_EQ(host_set_.hosts_[1], lb->chooseHost(&context));
  }
  {
    // First attempt succeeds even when retry count is > 0.
    TestLoadBalancerContext context(10, 2, [](const Host&) { return false; });
    EXPECT_EQ(host_set_.hosts_[1], lb->chooseHost(&context));
  }
  {
    // Second attempt chooses a different host in the ring.
    TestLoadBalancerContext context(
        10, 2, [&](const Host& host) { return &host == host_set_.hosts_[1].get(); });
    EXPECT_EQ(host_set_.hosts_[0], lb->chooseHost(&context));
  }
  {
    // Exhausted retries return the last checked host.
    TestLoadBalancerContext context(10, 2, [](const Host&) { return true; });
    EXPECT_EQ(host_set_.hosts_[5], lb->chooseHost(&context));
  }
}

// Weighted sanity test.
TEST_F(MaglevLoadBalancerTest, Weighted) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90", 1),
                      makeTestHost(info_, "tcp://127.0.0.1:91", 2)};
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks({}, {});
  init(17);
  EXPECT_EQ(6, lb_->stats().min_entries_per_host_.value());
  EXPECT_EQ(11, lb_->stats().max_entries_per_host_.value());

  // maglev: i=0 host=127.0.0.1:91
  // maglev: i=1 host=127.0.0.1:90
  // maglev: i=2 host=127.0.0.1:90
  // maglev: i=3 host=127.0.0.1:91
  // maglev: i=4 host=127.0.0.1:90
  // maglev: i=5 host=127.0.0.1:91
  // maglev: i=6 host=127.0.0.1:91
  // maglev: i=7 host=127.0.0.1:90
  // maglev: i=8 host=127.0.0.1:91
  // maglev: i=9 host=127.0.0.1:91
  // maglev: i=10 host=127.0.0.1:91
  // maglev: i=11 host=127.0.0.1:91
  // maglev: i=12 host=127.0.0.1:91
  // maglev: i=13 host=127.0.0.1:90
  // maglev: i=14 host=127.0.0.1:91
  // maglev: i=15 host=127.0.0.1:90
  // maglev: i=16 host=127.0.0.1:91
  LoadBalancerPtr lb = lb_->factory()->create();
  const std::vector<uint32_t> expected_assignments{1, 0, 0, 1, 0, 1, 1, 0, 1,
                                                   1, 1, 1, 1, 0, 1, 0, 1};
  for (uint32_t i = 0; i < 3 * expected_assignments.size(); ++i) {
    TestLoadBalancerContext context(i);
    EXPECT_EQ(host_set_.hosts_[expected_assignments[i % expected_assignments.size()]],
              lb->chooseHost(&context));
  }
}

// Locality weighted sanity test when localities have the same weights. Host weights for hosts in
// different localities shouldn't matter.
TEST_F(MaglevLoadBalancerTest, LocalityWeightedSameLocalityWeights) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90", 1),
                      makeTestHost(info_, "tcp://127.0.0.1:91", 2)};
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.hosts_per_locality_ =
      makeHostsPerLocality({{host_set_.hosts_[0]}, {host_set_.hosts_[1]}});
  host_set_.healthy_hosts_per_locality_ = host_set_.hosts_per_locality_;
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 1}};
  host_set_.locality_weights_ = locality_weights;
  host_set_.runCallbacks({}, {});
  init(17);
  EXPECT_EQ(8, lb_->stats().min_entries_per_host_.value());
  EXPECT_EQ(9, lb_->stats().max_entries_per_host_.value());

  // maglev: i=0 host=127.0.0.1:91
  // maglev: i=1 host=127.0.0.1:90
  // maglev: i=2 host=127.0.0.1:90
  // maglev: i=3 host=127.0.0.1:91
  // maglev: i=4 host=127.0.0.1:90
  // maglev: i=5 host=127.0.0.1:91
  // maglev: i=6 host=127.0.0.1:91
  // maglev: i=7 host=127.0.0.1:90
  // maglev: i=8 host=127.0.0.1:90
  // maglev: i=9 host=127.0.0.1:91
  // maglev: i=10 host=127.0.0.1:90
  // maglev: i=11 host=127.0.0.1:91
  // maglev: i=12 host=127.0.0.1:90
  // maglev: i=13 host=127.0.0.1:90
  // maglev: i=14 host=127.0.0.1:91
  // maglev: i=15 host=127.0.0.1:90
  // maglev: i=16 host=127.0.0.1:91
  LoadBalancerPtr lb = lb_->factory()->create();
  const std::vector<uint32_t> expected_assignments{1, 0, 0, 1, 0, 1, 1, 0, 0,
                                                   1, 0, 1, 0, 0, 1, 0, 1};
  for (uint32_t i = 0; i < 3 * expected_assignments.size(); ++i) {
    TestLoadBalancerContext context(i);
    EXPECT_EQ(host_set_.hosts_[expected_assignments[i % expected_assignments.size()]],
              lb->chooseHost(&context));
  }
}

// Locality weighted sanity test when localities have different weights. Host weights for hosts in
// different localities shouldn't matter.
TEST_F(MaglevLoadBalancerTest, LocalityWeightedDifferentLocalityWeights) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90", 1),
                      makeTestHost(info_, "tcp://127.0.0.1:91", 2),
                      makeTestHost(info_, "tcp://127.0.0.1:92", 3)};
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.hosts_per_locality_ =
      makeHostsPerLocality({{host_set_.hosts_[0]}, {host_set_.hosts_[2]}, {host_set_.hosts_[1]}});
  host_set_.healthy_hosts_per_locality_ = host_set_.hosts_per_locality_;
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{8, 0, 2}};
  host_set_.locality_weights_ = locality_weights;
  host_set_.runCallbacks({}, {});
  init(17);
  EXPECT_EQ(4, lb_->stats().min_entries_per_host_.value());
  EXPECT_EQ(13, lb_->stats().max_entries_per_host_.value());

  // maglev: i=0 host=127.0.0.1:91
  // maglev: i=1 host=127.0.0.1:90
  // maglev: i=2 host=127.0.0.1:90
  // maglev: i=3 host=127.0.0.1:90
  // maglev: i=4 host=127.0.0.1:90
  // maglev: i=5 host=127.0.0.1:90
  // maglev: i=6 host=127.0.0.1:91
  // maglev: i=7 host=127.0.0.1:90
  // maglev: i=8 host=127.0.0.1:90
  // maglev: i=9 host=127.0.0.1:91
  // maglev: i=10 host=127.0.0.1:90
  // maglev: i=11 host=127.0.0.1:91
  // maglev: i=12 host=127.0.0.1:90
  // maglev: i=13 host=127.0.0.1:90
  // maglev: i=14 host=127.0.0.1:90
  // maglev: i=15 host=127.0.0.1:90
  // maglev: i=16 host=127.0.0.1:90
  LoadBalancerPtr lb = lb_->factory()->create();
  const std::vector<uint32_t> expected_assignments{1, 0, 0, 0, 0, 0, 1, 0, 0,
                                                   1, 0, 1, 0, 0, 0, 0, 0};
  for (uint32_t i = 0; i < 3 * expected_assignments.size(); ++i) {
    TestLoadBalancerContext context(i);
    EXPECT_EQ(host_set_.hosts_[expected_assignments[i % expected_assignments.size()]],
              lb->chooseHost(&context));
  }
}

// Locality weighted with all localities zero weighted.
TEST_F(MaglevLoadBalancerTest, LocalityWeightedAllZeroLocalityWeights) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90", 1)};
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.hosts_per_locality_ = makeHostsPerLocality({{host_set_.hosts_[0]}});
  host_set_.healthy_hosts_per_locality_ = host_set_.hosts_per_locality_;
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{0}};
  host_set_.locality_weights_ = locality_weights;
  host_set_.runCallbacks({}, {});
  init(17);
  LoadBalancerPtr lb = lb_->factory()->create();
  TestLoadBalancerContext context(0);
  EXPECT_EQ(nullptr, lb->chooseHost(&context));
}

// Validate that when we are in global panic and have localities, we get sane
// results (fall back to non-healthy hosts).
TEST_F(MaglevLoadBalancerTest, LocalityWeightedGlobalPanic) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90", 1),
                      makeTestHost(info_, "tcp://127.0.0.1:91", 2)};
  host_set_.healthy_hosts_ = {};
  host_set_.hosts_per_locality_ =
      makeHostsPerLocality({{host_set_.hosts_[0]}, {host_set_.hosts_[1]}});
  host_set_.healthy_hosts_per_locality_ = makeHostsPerLocality({{}, {}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 1}};
  host_set_.locality_weights_ = locality_weights;
  host_set_.runCallbacks({}, {});
  init(17);
  EXPECT_EQ(8, lb_->stats().min_entries_per_host_.value());
  EXPECT_EQ(9, lb_->stats().max_entries_per_host_.value());

  // maglev: i=0 host=127.0.0.1:91
  // maglev: i=1 host=127.0.0.1:90
  // maglev: i=2 host=127.0.0.1:90
  // maglev: i=3 host=127.0.0.1:91
  // maglev: i=4 host=127.0.0.1:90
  // maglev: i=5 host=127.0.0.1:91
  // maglev: i=6 host=127.0.0.1:91
  // maglev: i=7 host=127.0.0.1:90
  // maglev: i=8 host=127.0.0.1:90
  // maglev: i=9 host=127.0.0.1:91
  // maglev: i=10 host=127.0.0.1:90
  // maglev: i=11 host=127.0.0.1:91
  // maglev: i=12 host=127.0.0.1:90
  // maglev: i=13 host=127.0.0.1:90
  // maglev: i=14 host=127.0.0.1:91
  // maglev: i=15 host=127.0.0.1:90
  // maglev: i=16 host=127.0.0.1:91
  LoadBalancerPtr lb = lb_->factory()->create();
  const std::vector<uint32_t> expected_assignments{1, 0, 0, 1, 0, 1, 1, 0, 0,
                                                   1, 0, 1, 0, 0, 1, 0, 1};
  for (uint32_t i = 0; i < 3 * expected_assignments.size(); ++i) {
    TestLoadBalancerContext context(i);
    EXPECT_EQ(host_set_.hosts_[expected_assignments[i % expected_assignments.size()]],
              lb->chooseHost(&context));
  }
}

// Given extremely lopsided locality weights, and a table that isn't large enough to fit all hosts,
// expect that the least-weighted hosts appear once, and the most-weighted host fills the remainder.
TEST_F(MaglevLoadBalancerTest, LocalityWeightedLopsided) {
  host_set_.hosts_.clear();
  HostVector heavy_but_sparse, light_but_dense;
  for (uint32_t i = 0; i < 1024; ++i) {
    auto host(makeTestHost(info_, fmt::format("tcp://127.0.0.1:{}", i)));
    host_set_.hosts_.push_back(host);
    (i == 0 ? heavy_but_sparse : light_but_dense).push_back(host);
  }
  host_set_.healthy_hosts_ = {};
  host_set_.hosts_per_locality_ = makeHostsPerLocality({heavy_but_sparse, light_but_dense});
  host_set_.healthy_hosts_per_locality_ = host_set_.hosts_per_locality_;
  host_set_.locality_weights_ = makeLocalityWeights({127, 1});
  host_set_.runCallbacks({}, {});
  init(MaglevTable::DefaultTableSize);
  EXPECT_EQ(1, lb_->stats().min_entries_per_host_.value());
  EXPECT_EQ(MaglevTable::DefaultTableSize - 1023, lb_->stats().max_entries_per_host_.value());

  LoadBalancerPtr lb = lb_->factory()->create();

  // Populate a histogram of the number of table entries for each host...
  uint32_t counts[1024] = {0};
  for (uint32_t i = 0; i < MaglevTable::DefaultTableSize; ++i) {
    TestLoadBalancerContext context(i);
    uint32_t port = lb->chooseHost(&context)->address()->ip()->port();
    ++counts[port];
  }

  // Each of the light_but_dense hosts should appear in the table once.
  for (uint32_t i = 1; i < 1024; ++i) {
    EXPECT_EQ(1, counts[i]);
  }

  // The heavy_but_sparse host should occupy the remainder of the table.
  EXPECT_EQ(MaglevTable::DefaultTableSize - 1023, counts[0]);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
