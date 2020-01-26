#include <memory>

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "common/upstream/maglev_lb.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/mocks.h"

namespace Envoy {
namespace Upstream {
namespace {

class TestLoadBalancerContext : public LoadBalancerContextBase {
public:
  TestLoadBalancerContext(uint64_t hash_key) : hash_key_(hash_key) {}

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return hash_key_; }

  absl::optional<uint64_t> hash_key_;
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
  NiceMock<Runtime::MockRandomGenerator> random_;
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
