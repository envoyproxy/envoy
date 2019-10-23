#include <cstdint>
#include <limits>
#include <memory>
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

class RingHashLoadBalancerTest : public testing::TestWithParam<bool> {
public:
  RingHashLoadBalancerTest() : stats_(ClusterInfoImpl::generateStats(stats_store_)) {}

  void init() {
    lb_ = std::make_unique<RingHashLoadBalancer>(priority_set_, stats_, stats_store_, runtime_,
                                                 random_, config_, common_config_);
    lb_->initialize();
  }

  // Run all tests against both priority 0 and priority 1 host sets, to ensure
  // all the load balancers have equivalent functionality for failover host sets.
  MockHostSet& hostSet() { return GetParam() ? host_set_ : failover_host_set_; }

  NiceMock<MockPrioritySet> priority_set_;
  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  MockHostSet& failover_host_set_ = *priority_set_.getMockHostSet(1);
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  Stats::IsolatedStoreImpl stats_store_;
  ClusterStats stats_;
  absl::optional<envoy::api::v2::Cluster::RingHashLbConfig> config_;
  envoy::api::v2::Cluster::CommonLbConfig common_config_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::unique_ptr<RingHashLoadBalancer> lb_;
};

// For tests which don't need to be run in both primary and failover modes.
using RingHashFailoverTest = RingHashLoadBalancerTest;

INSTANTIATE_TEST_SUITE_P(RingHashPrimaryOrFailover, RingHashLoadBalancerTest,
                         ::testing::Values(true, false));
INSTANTIATE_TEST_SUITE_P(RingHashPrimaryOrFailover, RingHashFailoverTest, ::testing::Values(true));

// Given no hosts, expect chooseHost to return null.
TEST_P(RingHashLoadBalancerTest, NoHost) {
  init();
  EXPECT_EQ(nullptr, lb_->factory()->create()->chooseHost(nullptr));
};

// Given minimum_ring_size > maximum_ring_size, expect an exception.
TEST_P(RingHashLoadBalancerTest, BadRingSizeBounds) {
  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().mutable_minimum_ring_size()->set_value(20);
  config_.value().mutable_maximum_ring_size()->set_value(10);
  EXPECT_THROW_WITH_MESSAGE(init(), EnvoyException,
                            "ring hash: minimum_ring_size (20) > maximum_ring_size (10)");
}

TEST_P(RingHashLoadBalancerTest, Basic) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93"),
      makeTestHost(info_, "tcp://127.0.0.1:94"), makeTestHost(info_, "tcp://127.0.0.1:95")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().mutable_minimum_ring_size()->set_value(12);

  init();
  EXPECT_EQ("ring_hash_lb.size", lb_->stats().size_.name());
  EXPECT_EQ("ring_hash_lb.min_hashes_per_host", lb_->stats().min_hashes_per_host_.name());
  EXPECT_EQ("ring_hash_lb.max_hashes_per_host", lb_->stats().max_hashes_per_host_.name());
  EXPECT_EQ(12, lb_->stats().size_.value());
  EXPECT_EQ(2, lb_->stats().min_hashes_per_host_.value());
  EXPECT_EQ(2, lb_->stats().max_hashes_per_host_.value());

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

  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().mutable_minimum_ring_size()->set_value(12);
  init();
  EXPECT_EQ(12, lb_->stats().size_.value());
  EXPECT_EQ(12, lb_->stats().min_hashes_per_host_.value());
  EXPECT_EQ(12, lb_->stats().max_hashes_per_host_.value());

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

// Expect reasonable results with Murmur2 hash.
TEST_P(RingHashLoadBalancerTest, BasicWithMurmur2) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:80"), makeTestHost(info_, "tcp://127.0.0.1:81"),
      makeTestHost(info_, "tcp://127.0.0.1:82"), makeTestHost(info_, "tcp://127.0.0.1:83"),
      makeTestHost(info_, "tcp://127.0.0.1:84"), makeTestHost(info_, "tcp://127.0.0.1:85")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().set_hash_function(envoy::api::v2::Cluster_RingHashLbConfig_HashFunction::
                                        Cluster_RingHashLbConfig_HashFunction_MURMUR_HASH_2);
  config_.value().mutable_minimum_ring_size()->set_value(12);
  init();
  EXPECT_EQ(12, lb_->stats().size_.value());
  EXPECT_EQ(2, lb_->stats().min_hashes_per_host_.value());
  EXPECT_EQ(2, lb_->stats().max_hashes_per_host_.value());

  // This is the hash ring built using murmur2 hash.
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

// Given 2 hosts and a minimum ring size of 3, expect 2 hashes per host and a ring size of 4.
TEST_P(RingHashLoadBalancerTest, UnevenHosts) {
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80"),
                      makeTestHost(info_, "tcp://127.0.0.1:81")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().mutable_minimum_ring_size()->set_value(3);
  init();
  EXPECT_EQ(4, lb_->stats().size_.value());
  EXPECT_EQ(2, lb_->stats().min_hashes_per_host_.value());
  EXPECT_EQ(2, lb_->stats().max_hashes_per_host_.value());

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

// Given hosts with weights 1, 2 and 3, and a ring size of exactly 6, expect the correct number of
// hashes for each host.
TEST_P(RingHashLoadBalancerTest, HostWeightedTinyRing) {
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90", 1),
                      makeTestHost(info_, "tcp://127.0.0.1:91", 2),
                      makeTestHost(info_, "tcp://127.0.0.1:92", 3)};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  // enforce a ring size of exactly six entries
  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().mutable_minimum_ring_size()->set_value(6);
  config_.value().mutable_maximum_ring_size()->set_value(6);
  init();
  EXPECT_EQ(6, lb_->stats().size_.value());
  EXPECT_EQ(1, lb_->stats().min_hashes_per_host_.value());
  EXPECT_EQ(3, lb_->stats().max_hashes_per_host_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // :90 should appear once, :91 should appear twice and :92 should appear three times.
  std::unordered_map<uint64_t, uint32_t> expected{
      {928266305478181108UL, 2},  {4443673547860492590UL, 2},  {5583722120771150861UL, 1},
      {6311230543546372928UL, 1}, {13444792449719432967UL, 2}, {16117243373044804889UL, 0}};
  for (const auto& entry : expected) {
    TestLoadBalancerContext context(entry.first);
    EXPECT_EQ(hostSet().hosts_[entry.second], lb->chooseHost(&context));
  }
}

// Given hosts with weights 1, 2 and 3, and a sufficiently large ring, expect that requests will
// distribute to the hosts with approximately the right proportion.
TEST_P(RingHashLoadBalancerTest, HostWeightedLargeRing) {
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90", 1),
                      makeTestHost(info_, "tcp://127.0.0.1:91", 2),
                      makeTestHost(info_, "tcp://127.0.0.1:92", 3)};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().mutable_minimum_ring_size()->set_value(6144);
  init();
  EXPECT_EQ(6144, lb_->stats().size_.value());
  EXPECT_EQ(1024, lb_->stats().min_hashes_per_host_.value());
  EXPECT_EQ(3072, lb_->stats().max_hashes_per_host_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // Generate 6000 hashes around the ring and populate a histogram of which hosts they mapped to...
  uint32_t counts[3] = {0};
  for (uint32_t i = 0; i < 6000; ++i) {
    TestLoadBalancerContext context(i * (std::numeric_limits<uint64_t>::max() / 6000));
    uint32_t port = lb->chooseHost(&context)->address()->ip()->port();
    ++counts[port - 90];
  }

  EXPECT_EQ(987, counts[0]);  // :90 | ~1000 expected hits
  EXPECT_EQ(1932, counts[1]); // :91 | ~2000 expected hits
  EXPECT_EQ(3081, counts[2]); // :92 | ~3000 expected hits
}

// Given locality weights all 0, expect the same behavior as if no hosts were provided at all.
TEST_P(RingHashLoadBalancerTest, ZeroLocalityWeights) {
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90"),
                      makeTestHost(info_, "tcp://127.0.0.1:91")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().hosts_per_locality_ =
      makeHostsPerLocality({{hostSet().hosts_[0]}, {hostSet().hosts_[1]}});
  hostSet().healthy_hosts_per_locality_ = hostSet().hosts_per_locality_;
  hostSet().locality_weights_ = makeLocalityWeights({0, 0});
  hostSet().runCallbacks({}, {});

  init();
  EXPECT_EQ(nullptr, lb_->factory()->create()->chooseHost(nullptr));
}

// Given localities with weights 1, 2, 3 and 0, and a ring size of exactly 6, expect the correct
// number of hashes for each host.
TEST_P(RingHashLoadBalancerTest, LocalityWeightedTinyRing) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().hosts_per_locality_ = makeHostsPerLocality(
      {{hostSet().hosts_[0]}, {hostSet().hosts_[1]}, {hostSet().hosts_[2]}, {hostSet().hosts_[3]}});
  hostSet().healthy_hosts_per_locality_ = hostSet().hosts_per_locality_;
  hostSet().locality_weights_ = makeLocalityWeights({1, 2, 3, 0});
  hostSet().runCallbacks({}, {});

  // enforce a ring size of exactly six entries
  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().mutable_minimum_ring_size()->set_value(6);
  config_.value().mutable_maximum_ring_size()->set_value(6);
  init();
  EXPECT_EQ(6, lb_->stats().size_.value());
  EXPECT_EQ(1, lb_->stats().min_hashes_per_host_.value());
  EXPECT_EQ(3, lb_->stats().max_hashes_per_host_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // :90 should appear once, :91 should appear twice, :92 should appear three times,
  // and :93 shouldn't appear at all.
  std::unordered_map<uint64_t, uint32_t> expected{
      {928266305478181108UL, 2},  {4443673547860492590UL, 2},  {5583722120771150861UL, 1},
      {6311230543546372928UL, 1}, {13444792449719432967UL, 2}, {16117243373044804889UL, 0}};
  for (const auto& entry : expected) {
    TestLoadBalancerContext context(entry.first);
    EXPECT_EQ(hostSet().hosts_[entry.second], lb->chooseHost(&context));
  }
}

// Given localities with weights 1, 2, 3 and 0, and a sufficiently large ring, expect that requests
// will distribute to the hosts with approximately the right proportion.
TEST_P(RingHashLoadBalancerTest, LocalityWeightedLargeRing) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().hosts_per_locality_ = makeHostsPerLocality(
      {{hostSet().hosts_[0]}, {hostSet().hosts_[1]}, {hostSet().hosts_[2]}, {hostSet().hosts_[3]}});
  hostSet().healthy_hosts_per_locality_ = hostSet().hosts_per_locality_;
  hostSet().locality_weights_ = makeLocalityWeights({1, 2, 3, 0});
  hostSet().runCallbacks({}, {});

  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().mutable_minimum_ring_size()->set_value(6144);
  init();
  EXPECT_EQ(6144, lb_->stats().size_.value());
  EXPECT_EQ(1024, lb_->stats().min_hashes_per_host_.value());
  EXPECT_EQ(3072, lb_->stats().max_hashes_per_host_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // Generate 6000 hashes around the ring and populate a histogram of which hosts they mapped to...
  uint32_t counts[4] = {0};
  for (uint32_t i = 0; i < 6000; ++i) {
    TestLoadBalancerContext context(i * (std::numeric_limits<uint64_t>::max() / 6000));
    uint32_t port = lb->chooseHost(&context)->address()->ip()->port();
    ++counts[port - 90];
  }

  EXPECT_EQ(987, counts[0]);  // :90 | ~1000 expected hits
  EXPECT_EQ(1932, counts[1]); // :91 | ~2000 expected hits
  EXPECT_EQ(3081, counts[2]); // :92 | ~3000 expected hits
  EXPECT_EQ(0, counts[3]);    // :93 |    =0 expected hits
}

// Given both host weights and locality weights, expect the correct number of hashes for each host.
TEST_P(RingHashLoadBalancerTest, HostAndLocalityWeightedTinyRing) {
  // :90 and :91 have a 1:2 ratio within the first locality, :92 and :93 have a 1:2 ratio within the
  // second locality, and the two localities have a 1:2 ratio overall.
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90", 1), makeTestHost(info_, "tcp://127.0.0.1:91", 2),
      makeTestHost(info_, "tcp://127.0.0.1:92", 1), makeTestHost(info_, "tcp://127.0.0.1:93", 2)};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().hosts_per_locality_ = makeHostsPerLocality(
      {{hostSet().hosts_[0], hostSet().hosts_[1]}, {hostSet().hosts_[2], hostSet().hosts_[3]}});
  hostSet().healthy_hosts_per_locality_ = hostSet().hosts_per_locality_;
  hostSet().locality_weights_ = makeLocalityWeights({1, 2});
  hostSet().runCallbacks({}, {});

  // enforce a ring size of exactly 9 entries
  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().mutable_minimum_ring_size()->set_value(9);
  config_.value().mutable_maximum_ring_size()->set_value(9);
  init();
  EXPECT_EQ(9, lb_->stats().size_.value());
  EXPECT_EQ(1, lb_->stats().min_hashes_per_host_.value());
  EXPECT_EQ(4, lb_->stats().max_hashes_per_host_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // :90 should appear once, :91 and :92 should each appear two times, and :93 should appear four
  // times, to get the correct overall proportions.
  std::unordered_map<uint64_t, uint32_t> expected{
      {928266305478181108UL, 2},   {3851675632748031481UL, 3},  {5583722120771150861UL, 1},
      {6311230543546372928UL, 1},  {7700377290971790572UL, 3},  {12559126875973811811UL, 3},
      {13444792449719432967UL, 2}, {13784988426630141778UL, 3}, {16117243373044804889UL, 0}};
  for (const auto& entry : expected) {
    TestLoadBalancerContext context(entry.first);
    EXPECT_EQ(hostSet().hosts_[entry.second], lb->chooseHost(&context));
  }
}

// Given both host weights and locality weights, and a sufficiently large ring, expect that requests
// will distribute to the hosts with approximately the right proportion.
TEST_P(RingHashLoadBalancerTest, HostAndLocalityWeightedLargeRing) {
  // :90 and :91 have a 1:2 ratio within the first locality, :92 and :93 have a 1:2 ratio within the
  // second locality, and the two localities have a 1:2 ratio overall.
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90", 1), makeTestHost(info_, "tcp://127.0.0.1:91", 2),
      makeTestHost(info_, "tcp://127.0.0.1:92", 1), makeTestHost(info_, "tcp://127.0.0.1:93", 2)};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().hosts_per_locality_ = makeHostsPerLocality(
      {{hostSet().hosts_[0], hostSet().hosts_[1]}, {hostSet().hosts_[2], hostSet().hosts_[3]}});
  hostSet().healthy_hosts_per_locality_ = hostSet().hosts_per_locality_;
  hostSet().locality_weights_ = makeLocalityWeights({1, 2});
  hostSet().runCallbacks({}, {});

  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().mutable_minimum_ring_size()->set_value(9216);
  init();
  EXPECT_EQ(9216, lb_->stats().size_.value());
  EXPECT_EQ(1024, lb_->stats().min_hashes_per_host_.value());
  EXPECT_EQ(4096, lb_->stats().max_hashes_per_host_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // Generate 9000 hashes around the ring and populate a histogram of which hosts they mapped to...
  uint32_t counts[4] = {0};
  for (uint32_t i = 0; i < 9000; ++i) {
    TestLoadBalancerContext context(i * (std::numeric_limits<uint64_t>::max() / 9000));
    uint32_t port = lb->chooseHost(&context)->address()->ip()->port();
    ++counts[port - 90];
  }

  EXPECT_EQ(924, counts[0]);  // :90 | ~1000 expected hits
  EXPECT_EQ(2009, counts[1]); // :91 | ~2000 expected hits
  EXPECT_EQ(2053, counts[2]); // :92 | ~2000 expected hits
  EXPECT_EQ(4014, counts[3]); // :93 | ~4000 expected hits
}

// Given 4 hosts and a ring size of exactly 2, expect that 2 hosts will be present in the ring and
// the other 2 hosts will be absent.
TEST_P(RingHashLoadBalancerTest, SmallFractionalScale) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().mutable_minimum_ring_size()->set_value(2);
  config_.value().mutable_maximum_ring_size()->set_value(2);
  init();
  EXPECT_EQ(2, lb_->stats().size_.value());
  EXPECT_EQ(0, lb_->stats().min_hashes_per_host_.value());
  EXPECT_EQ(1, lb_->stats().max_hashes_per_host_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // Generate some reasonable number of hashes around the ring and populate a histogram of which
  // hosts they mapped to. Here we don't care about the distribution (because the scale is
  // intentionally stupidly low), other than to verify that two of the hosts are absent.
  uint32_t counts[4] = {0};
  for (uint32_t i = 0; i < 1024; ++i) {
    TestLoadBalancerContext context(i * (std::numeric_limits<uint64_t>::max() / 1024));
    uint32_t port = lb->chooseHost(&context)->address()->ip()->port();
    ++counts[port - 90];
  }

  uint32_t zeroes = 0;
  uint32_t sum = 0;
  for (auto count : counts) {
    if (count == 0) {
      ++zeroes;
    } else {
      sum += count;
    }
  }
  EXPECT_EQ(2, zeroes); // two hosts (we don't care which ones) should get no traffic
  EXPECT_EQ(1024, sum); // the other two hosts should get all the traffic
}

// Given 2 hosts and a ring size of exactly 1023, expect that one host will have 511 entries and the
// other will have 512.
TEST_P(RingHashLoadBalancerTest, LargeFractionalScale) {
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90"),
                      makeTestHost(info_, "tcp://127.0.0.1:91")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().mutable_minimum_ring_size()->set_value(1023);
  config_.value().mutable_maximum_ring_size()->set_value(1023);
  init();
  EXPECT_EQ(1023, lb_->stats().size_.value());
  EXPECT_EQ(511, lb_->stats().min_hashes_per_host_.value());
  EXPECT_EQ(512, lb_->stats().max_hashes_per_host_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // Generate 1023 hashes around the ring and populate a histogram of which hosts they mapped to...
  uint32_t counts[2] = {0};
  for (uint32_t i = 0; i < 1023; ++i) {
    TestLoadBalancerContext context(i * (std::numeric_limits<uint64_t>::max() / 1023));
    uint32_t port = lb->chooseHost(&context)->address()->ip()->port();
    ++counts[port - 90];
  }

  EXPECT_EQ(526, counts[0]); // :90 | ~512 expected hits
  EXPECT_EQ(497, counts[1]); // :91 | ~511 expected hits
}

// Given extremely lopsided locality weights, and a ring that isn't large enough to fit all hosts,
// expect that the correct proportion of hosts will be present in the ring.
TEST_P(RingHashLoadBalancerTest, LopsidedWeightSmallScale) {
  hostSet().hosts_.clear();
  HostVector heavy_but_sparse, light_but_dense;
  for (uint32_t i = 0; i < 1024; ++i) {
    auto host(makeTestHost(info_, fmt::format("tcp://127.0.0.1:{}", i)));
    hostSet().hosts_.push_back(host);
    (i == 0 ? heavy_but_sparse : light_but_dense).push_back(host);
  }
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().hosts_per_locality_ = makeHostsPerLocality({heavy_but_sparse, light_but_dense});
  hostSet().healthy_hosts_per_locality_ = hostSet().hosts_per_locality_;
  hostSet().locality_weights_ = makeLocalityWeights({127, 1});
  hostSet().runCallbacks({}, {});

  config_ = envoy::api::v2::Cluster::RingHashLbConfig();
  config_.value().mutable_minimum_ring_size()->set_value(1024);
  config_.value().mutable_maximum_ring_size()->set_value(1024);
  init();
  EXPECT_EQ(1024, lb_->stats().size_.value());
  EXPECT_EQ(0, lb_->stats().min_hashes_per_host_.value());
  // Host :0, from the heavy-but-sparse locality, should have 1016 out of the 1024 entries on the
  // ring, which gives us the right ratio of 127/128.
  EXPECT_EQ(1016, lb_->stats().max_hashes_per_host_.value());
  LoadBalancerPtr lb = lb_->factory()->create();

  // Every 128th host in the light-but-dense locality should have an entry on the ring, for a total
  // of 8 entries. This gives us the right ratio of 1/128.
  std::unordered_map<uint64_t, uint32_t> expected{
      {11664790346325243808UL, 1},   {15894554872961148518UL, 128}, {13958138884277627155UL, 256},
      {15803774069438192949UL, 384}, {3829253010855396576UL, 512},  {17918147347826565154UL, 640},
      {6442769608292299103UL, 768},  {5881074926069334434UL, 896}};
  for (const auto& entry : expected) {
    TestLoadBalancerContext context(entry.first);
    EXPECT_EQ(hostSet().hosts_[entry.second], lb->chooseHost(&context));
  }
}

} // namespace
} // namespace Upstream
} // namespace Envoy
