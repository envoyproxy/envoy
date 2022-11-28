#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <string>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/load_balancing_policies/deterministic_aperture/v3/deterministic_aperture.pb.h"

#include "source/common/network/utility.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/load_balancing_policies/deterministic_aperture/load_balancer.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/simulated_time_system.h"

#include "absl/container/node_hash_map.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace DeterministicAperture {

class TestLoadBalancerContext : public Upstream::LoadBalancerContextBase {
public:
  using HostPredicate = std::function<bool(const Upstream::Host&)>;

  TestLoadBalancerContext()
      : TestLoadBalancerContext(0, [](const Upstream::Host&) { return false; }) {}
  TestLoadBalancerContext(uint32_t retry_count, HostPredicate should_select_another_host)
      : retry_count_(retry_count), should_select_another_host_(should_select_another_host) {}

  // Upstream::LoadBalancerContext
  absl::optional<uint64_t> computeHashKey() override { return 0; }
  uint32_t hostSelectionRetryCount() const override { return retry_count_; };
  bool shouldSelectAnotherHost(const Upstream::Host& host) override {
    return should_select_another_host_(host);
  }

  uint32_t retry_count_;
  HostPredicate should_select_another_host_;
};

class LoadBalancerTest : public Event::TestUsingSimulatedTime, public testing::TestWithParam<bool> {
public:
  LoadBalancerTest()
      : stat_names_(stats_store_.symbolTable()), stats_(stat_names_, stats_store_),
        rng_(random_dev_()), random_distribution_(0, 1) {}

  void init() {
    lb_ = std::make_unique<LoadBalancer>(priority_set_, stats_, stats_store_, runtime_, random_,
                                         config_, common_config_);
    lb_->initialize();
  }

  // Run all tests against both priority 0 and priority 1 host sets, to ensure
  // all the load balancers have equivalent functionality for failover host sets.
  Upstream::MockHostSet& hostSet() { return GetParam() ? host_set_ : failover_host_set_; }

  bool nearlyEqual(double a, double b) const { return fabs(a - b) < 0.005f; }

  NiceMock<Upstream::MockPrioritySet> priority_set_;
  Upstream::MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  Upstream::MockHostSet& failover_host_set_ = *priority_set_.getMockHostSet(1);
  std::shared_ptr<Upstream::MockClusterInfo> info_{new NiceMock<Upstream::MockClusterInfo>()};
  Stats::IsolatedStoreImpl stats_store_;
  Upstream::ClusterLbStatNames stat_names_;
  Upstream::ClusterLbStats stats_;
  absl::optional<envoy::extensions::load_balancing_policies::deterministic_aperture::v3::
                     DeterministicApertureLbConfig>
      config_;
  envoy::config::cluster::v3::Cluster::CommonLbConfig common_config_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  std::unique_ptr<DeterministicAperture::LoadBalancer> lb_;
  std::random_device random_dev_;
  mutable std::mt19937 rng_;
  mutable std::uniform_real_distribution<double> random_distribution_;
};

// For tests which don't need to be run in both primary and failover modes.
using DeterministicApertureFailoverTest = LoadBalancerTest;

INSTANTIATE_TEST_SUITE_P(DeterministicAperturePrimaryOrFailover, LoadBalancerTest,
                         ::testing::Values(true, false));
INSTANTIATE_TEST_SUITE_P(DeterministicAperturePrimaryOrFailover, DeterministicApertureFailoverTest,
                         ::testing::Values(true));

// Given no hosts, expect chooseHost to return null.
TEST_P(LoadBalancerTest, NoHost) {
  config_ = envoy::extensions::load_balancing_policies::deterministic_aperture::v3::
      DeterministicApertureLbConfig();
  init();
  EXPECT_EQ(nullptr, lb_->factory()->create()->chooseHost(nullptr));

  EXPECT_EQ(nullptr, lb_->factory()->create()->peekAnotherHost(nullptr));
  EXPECT_FALSE(lb_->factory()->create()->lifetimeCallbacks().has_value());
  std::vector<uint8_t> hash_key;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  EXPECT_FALSE(lb_->factory()
                   ->create()
                   ->selectExistingConnection(nullptr, *mock_host, hash_key)
                   .has_value());
}

TEST_P(LoadBalancerTest, BaseMethods) {
  init();
  EXPECT_EQ(nullptr, lb_->peekAnotherHost(nullptr));
  EXPECT_FALSE(lb_->lifetimeCallbacks().has_value());
  std::vector<uint8_t> hash_key;
  auto mock_host = std::make_shared<NiceMock<Upstream::MockHost>>();
  EXPECT_FALSE(lb_->selectExistingConnection(nullptr, *mock_host, hash_key).has_value());
};

// Test for thread aware load balancer destructed before load balancer factory. After CDS removes a
// cluster, the operation does not immediately reach the worker thread. There may be cases where the
// thread aware load balancer is destructed, but the load balancer factory is still used in the
// worker thread.
TEST_P(LoadBalancerTest, LbDestructedBeforeFactory) {
  init();

  auto factory = lb_->factory();
  lb_.reset();

  EXPECT_NE(nullptr, factory->create());
}

TEST_P(LoadBalancerTest, Basic) {
  hostSet().hosts_ = {Upstream::makeTestHost(info_, "tcp://127.0.0.1:90", simTime()),
                      Upstream::makeTestHost(info_, "tcp://127.0.0.1:91", simTime()),
                      Upstream::makeTestHost(info_, "tcp://127.0.0.1:92", simTime()),
                      Upstream::makeTestHost(info_, "tcp://127.0.0.1:93", simTime()),
                      Upstream::makeTestHost(info_, "tcp://127.0.0.1:94", simTime()),
                      Upstream::makeTestHost(info_, "tcp://127.0.0.1:95", simTime())};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_ = envoy::extensions::load_balancing_policies::deterministic_aperture::v3::
      DeterministicApertureLbConfig();
  config_->mutable_minimum_ring_size()->set_value(8);
  config_->mutable_maximum_ring_size()->set_value(8);

  // hash ring:
  // port | position
  // ---------------------------
  // :92  | 928266305478181108
  // :90  | 1033482794131418490
  // :95  | 3551244743356806947
  // :93  | 3851675632748031481
  // :91  | 6311230543546372928
  // :93  | 7700377290971790572
  // :94  | 15516499411664133160
  // :90  | 16117243373044804889

  std::vector<size_t> expected_hosts = {2, 0, 5, 3, 1, 3, 4, 0};

  config_->set_total_peers(8);
  for (size_t i = 0; i < 1; ++i) {
    for (uint32_t peer_index = 0; peer_index < 8; ++peer_index) {
      config_->set_peer_index(peer_index);
      init();

      EXPECT_EQ("deterministic_aperture_lb.size", lb_->ringStats().size_.name());
      EXPECT_EQ("deterministic_aperture_lb.min_hashes_per_host",
                lb_->ringStats().min_hashes_per_host_.name());
      EXPECT_EQ("deterministic_aperture_lb.max_hashes_per_host",
                lb_->ringStats().max_hashes_per_host_.name());
      EXPECT_EQ(8, lb_->ringStats().size_.value());
      EXPECT_EQ(1, lb_->ringStats().min_hashes_per_host_.value());
      EXPECT_EQ(2, lb_->ringStats().max_hashes_per_host_.value());

      Upstream::LoadBalancerPtr lb = lb_->factory()->create();

      TestLoadBalancerContext context;
      EXPECT_EQ(hostSet().hosts_[expected_hosts[peer_index]], lb->chooseHost(&context));
    }
  }
}

TEST_P(LoadBalancerTest, RingPick2) {
  const uint64_t ring_size = 10;
  const auto hash_function = Upstream::Ring::HashFunction::MURMUR_HASH_2;
  const double peer_offset = random_distribution_(rng_);
  double peer_width = random_distribution_(rng_);

  if (peer_width < 0.4) {
    peer_width += 0.4;
  }

  Upstream::NormalizedHostWeightVector normalized_host_weights;

  for (uint64_t i = 0; i < ring_size; ++i) {
    auto host = Upstream::makeTestHost(info_, absl::StrCat("tcp://127.0.0.1:", i), simTime());
    normalized_host_weights.push_back({host, 0.1});
  }

  auto scope = stats_store_.createScope("ring_hash.");
  auto ring_hash_lb = std::make_shared<LoadBalancer::Ring>(
      peer_offset, peer_width, normalized_host_weights, 0.1, ring_size, ring_size, hash_function,
      false, scope, Upstream::Ring::generateStats(*scope));

  absl::flat_hash_map<size_t, size_t> index_count;
  absl::flat_hash_map<size_t, double> index_weight;

  for (int i = 0; i < 100; ++i) {
    auto res = ring_hash_lb->pick2();

    absl::optional<double> wt1 = ring_hash_lb->weight(res.first, peer_offset, peer_width);
    absl::optional<double> wt2 = ring_hash_lb->weight(res.second, peer_offset, peer_width);

    index_count[res.first]++;
    index_count[res.second]++;

    index_weight[res.first] = *wt1;
    index_weight[res.second] = *wt2;
  }

  size_t total_picks_non_fractional = 0;
  size_t total_picks_fractional = 0;

  for (size_t i = 0; i < ring_size; ++i) {
    if (index_count.count(i) == 0) {
      continue;
    }
    if (nearlyEqual(index_weight[i], 1.0)) {
      total_picks_non_fractional += index_count[i];
    } else {
      total_picks_fractional += index_count[i];
    }
  }

  ASSERT_LE(total_picks_fractional, total_picks_non_fractional);
}

// Ensure if all the hosts with priority 0 unhealthy, the next priority hosts are used.
TEST_P(DeterministicApertureFailoverTest, BasicFailover) {
  host_set_.hosts_ = {Upstream::makeTestHost(info_, "tcp://127.0.0.1:80", simTime())};
  failover_host_set_.healthy_hosts_ = {
      Upstream::makeTestHost(info_, "tcp://127.0.0.1:82", simTime())};
  failover_host_set_.hosts_ = failover_host_set_.healthy_hosts_;

  config_ = envoy::extensions::load_balancing_policies::deterministic_aperture::v3::
      DeterministicApertureLbConfig();
  config_->mutable_minimum_ring_size()->set_value(12);
  config_->mutable_maximum_ring_size()->set_value(12);
  config_->set_total_peers(12);
  config_->set_peer_index(0);
  init();
  EXPECT_EQ(12, lb_->ringStats().size_.value());
  EXPECT_EQ(12, lb_->ringStats().min_hashes_per_host_.value());
  EXPECT_EQ(12, lb_->ringStats().max_hashes_per_host_.value());

  Upstream::LoadBalancerPtr lb = lb_->factory()->create();
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
  host_set_.hosts_ = {Upstream::makeTestHost(info_, "tcp://127.0.0.1:80", simTime()),
                      Upstream::makeTestHost(info_, "tcp://127.0.0.1:81", simTime())};
  host_set_.healthy_hosts_ = {host_set_.hosts_[0]};
  host_set_.runCallbacks({}, {});
  lb = lb_->factory()->create();
  EXPECT_CALL(random_, random()).WillOnce(Return(69));
  EXPECT_EQ(host_set_.healthy_hosts_[0], lb->chooseHost(nullptr));
  EXPECT_CALL(random_, random()).WillOnce(Return(71));
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb->chooseHost(nullptr));
}

} // namespace DeterministicAperture
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
