#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/load_balancing_policies/rendezvous_hash/rendezvous_hash_lb.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/priority_set.h"

#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Upstream {

// Test helper class to access private static methods of RendezvousHashTable.
// This class must be in the Envoy::Upstream namespace (not anonymous) so that
// the friend declaration in the header can find it.
class RendezvousHashMathTest : public testing::Test {
public:
  // Expose private static methods for testing.
  static double fastLog(double x) {
    return RendezvousHashLoadBalancer::RendezvousHashTable::fastLog(x);
  }

  static double fastLog2(double x) {
    return RendezvousHashLoadBalancer::RendezvousHashTable::fastLog2(x);
  }

  static double normalizeHash(uint64_t hash) {
    return RendezvousHashLoadBalancer::RendezvousHashTable::normalizeHash(hash);
  }

  static uint64_t xorshiftMult64(uint64_t x) {
    return RendezvousHashLoadBalancer::RendezvousHashTable::xorshiftMult64(x);
  }

  static double computeScore(uint64_t key, uint64_t host_hash, double weight) {
    return RendezvousHashLoadBalancer::RendezvousHashTable::computeScore(key, host_hash, weight);
  }
};

// Tests for fastLog - verify approximation accuracy against std::log.
TEST_F(RendezvousHashMathTest, FastLogBasicValues) {
  // Test common values and verify they're within acceptable error bounds.
  // For load balancing, relative error < 1% is acceptable.
  const double max_relative_error = 0.01;
  const double max_absolute_error = 0.01; // For values where expected ≈ 0

  // Test values across different ranges (excluding 1.0 where log=0).
  std::vector<double> test_values = {
      0.001, 0.01, 0.1, 0.25, 0.5,   0.75,   0.9, 0.99, 0.999, 1.5,   2.0,
      2.718, 3.0,  5.0, 10.0, 100.0, 1000.0, 1e6, 1e10, 1e-6,  1e-10,
  };

  for (double x : test_values) {
    double expected = std::log(x);
    double actual = fastLog(x);
    double relative_error = std::abs((actual - expected) / expected);
    EXPECT_LT(relative_error, max_relative_error)
        << "fastLog(" << x << ") = " << actual << ", std::log(" << x << ") = " << expected
        << ", relative error = " << relative_error;
  }

  // Special case: log(1) = 0, use absolute error.
  EXPECT_NEAR(fastLog(1.0), 0.0, max_absolute_error);
}

// Tests for fastLog2 - verify approximation accuracy against std::log2.
TEST_F(RendezvousHashMathTest, FastLog2BasicValues) {
  const double max_relative_error = 0.01;
  const double max_absolute_error = 0.01; // For values where expected ≈ 0

  // Test values across different ranges (excluding 1.0 where log2=0).
  std::vector<double> test_values = {
      0.001, 0.01, 0.1, 0.25, 0.5,  0.75,  0.9,    0.99, 0.999, 1.5,  2.0,   3.0,
      4.0,   5.0,  8.0, 10.0, 16.0, 100.0, 1000.0, 1e6,  1e10,  1e-6, 1e-10,
  };

  for (double x : test_values) {
    double expected = std::log2(x);
    double actual = fastLog2(x);
    double relative_error = std::abs((actual - expected) / expected);
    EXPECT_LT(relative_error, max_relative_error)
        << "fastLog2(" << x << ") = " << actual << ", std::log2(" << x << ") = " << expected
        << ", relative error = " << relative_error;
  }

  // Special case: log2(1) = 0, use absolute error.
  EXPECT_NEAR(fastLog2(1.0), 0.0, max_absolute_error);
}

// Test fastLog2 for powers of 2 - should be exact or very close.
TEST_F(RendezvousHashMathTest, FastLog2PowersOfTwo) {
  const double max_absolute_error = 0.01;

  for (int exp = -10; exp <= 10; ++exp) {
    double x = std::pow(2.0, exp);
    double expected = static_cast<double>(exp);
    double actual = fastLog2(x);
    EXPECT_NEAR(actual, expected, max_absolute_error)
        << "fastLog2(2^" << exp << ") = " << actual << ", expected = " << expected;
  }
}

// Test normalizeHash produces values in [0, 1).
TEST_F(RendezvousHashMathTest, NormalizeHashRange) {
  // Test boundary values and random values.
  std::vector<uint64_t> test_hashes = {
      0ULL,
      1ULL,
      1000ULL,
      std::numeric_limits<uint64_t>::max() / 2,
      std::numeric_limits<uint64_t>::max() - 1,
      std::numeric_limits<uint64_t>::max(),
      0x123456789ABCDEF0ULL,
      0xFEDCBA9876543210ULL,
  };

  for (uint64_t hash : test_hashes) {
    double normalized = normalizeHash(hash);
    EXPECT_GE(normalized, 0.0) << "normalizeHash(" << hash << ") = " << normalized;
    EXPECT_LT(normalized, 1.0) << "normalizeHash(" << hash << ") = " << normalized;
  }
}

// Test xorshiftMult64 produces non-zero outputs and good distribution.
TEST_F(RendezvousHashMathTest, XorshiftMult64NonZero) {
  // xorshift* should produce non-zero outputs for non-zero inputs.
  // (zero input produces zero output, which is fine for our use case).
  std::vector<uint64_t> test_inputs = {
      1ULL,
      12345ULL,
      std::numeric_limits<uint64_t>::max(),
      0xDEADBEEFCAFEBABEULL,
  };

  for (uint64_t input : test_inputs) {
    uint64_t output = xorshiftMult64(input);
    EXPECT_NE(output, 0ULL) << "xorshiftMult64(" << input << ") = 0";
    // Also verify it's different from input (mixing occurred).
    EXPECT_NE(output, input) << "xorshiftMult64(" << input << ") = input (no mixing)";
  }
}

// Test computeScore produces finite, positive scores for valid inputs.
TEST_F(RendezvousHashMathTest, ComputeScoreValidOutputs) {
  std::vector<uint64_t> keys = {0, 1, 12345, 0xFFFFFFFFFFFFFFFFULL};
  std::vector<uint64_t> host_hashes = {1, 12345, 0xDEADBEEFULL};
  std::vector<double> weights = {0.5, 1.0, 2.0, 10.0};

  for (uint64_t key : keys) {
    for (uint64_t host_hash : host_hashes) {
      for (double weight : weights) {
        double score = computeScore(key, host_hash, weight);
        EXPECT_TRUE(std::isfinite(score))
            << "computeScore(" << key << ", " << host_hash << ", " << weight << ") is not finite";
        EXPECT_GT(score, 0.0) << "computeScore(" << key << ", " << host_hash << ", " << weight
                              << ") <= 0";
      }
    }
  }
}

// Test that higher weights produce higher expected scores.
TEST_F(RendezvousHashMathTest, ComputeScoreWeightProportionality) {
  // For a fixed key and host_hash, higher weight should give higher score
  // (on average, though not guaranteed for every single hash).
  // We test with many different key/host combinations to verify the trend.
  uint64_t host_hash = 12345;
  double weight_low = 1.0;
  double weight_high = 10.0;

  int higher_score_count = 0;
  const int num_trials = 1000;

  for (int i = 0; i < num_trials; ++i) {
    uint64_t key = static_cast<uint64_t>(i) * 7919; // prime multiplier for spread
    double score_low = computeScore(key, host_hash, weight_low);
    double score_high = computeScore(key, host_hash, weight_high);

    if (score_high > score_low) {
      ++higher_score_count;
    }
  }

  // With weight ratio of 10:1, the higher weight should win most of the time.
  // Statistically, it should win ~10/11 ≈ 91% of the time.
  EXPECT_GT(higher_score_count, 800) << "Higher weight should usually produce higher score";
}

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

class RendezvousHashLoadBalancerTest : public testing::TestWithParam<bool> {
public:
  RendezvousHashLoadBalancerTest()
      : stat_names_(stats_store_.symbolTable()), stats_(stat_names_, *stats_store_.rootScope()) {}

  void init(bool locality_weighted_balancing = false) {
    if (locality_weighted_balancing) {
      config_.mutable_locality_weighted_lb_config();
    }

    absl::Status creation_status;
    TypedRendezvousHashLbConfig typed_config(config_, context_.regex_engine_, creation_status);
    ASSERT(creation_status.ok());

    lb_ = std::make_unique<RendezvousHashLoadBalancer>(
        priority_set_, stats_, *stats_store_.rootScope(), context_.runtime_loader_,
        context_.api_.random_, 50, typed_config.lb_config_, typed_config.hash_policy_);
    EXPECT_TRUE(lb_->initialize().ok());
  }

  // Run all tests against both priority 0 and priority 1 host sets, to ensure
  // all the load balancers have equivalent functionality for failover host sets.
  MockHostSet& hostSet() { return GetParam() ? host_set_ : failover_host_set_; }

  NiceMock<MockPrioritySet> priority_set_;

  // Just use this as parameters of create() method but thread aware load balancer will not use it.
  NiceMock<MockPrioritySet> worker_priority_set_;
  LoadBalancerParams lb_params_{worker_priority_set_, {}};

  MockHostSet& host_set_ = *priority_set_.getMockHostSet(0);
  MockHostSet& failover_host_set_ = *priority_set_.getMockHostSet(1);
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
  Stats::IsolatedStoreImpl stats_store_;
  ClusterLbStatNames stat_names_;
  ClusterLbStats stats_;
  envoy::extensions::load_balancing_policies::rendezvous_hash::v3::RendezvousHash config_;
  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  std::unique_ptr<RendezvousHashLoadBalancer> lb_;
};

// For tests which don't need to be run in both primary and failover modes.
using RendezvousHashFailoverTest = RendezvousHashLoadBalancerTest;

INSTANTIATE_TEST_SUITE_P(RendezvousHashPrimaryOrFailover, RendezvousHashLoadBalancerTest,
                         ::testing::Values(true, false));
INSTANTIATE_TEST_SUITE_P(RendezvousHashPrimaryOrFailover, RendezvousHashFailoverTest,
                         ::testing::Values(true));

TEST_P(RendezvousHashLoadBalancerTest, ChooseHostBeforeInit) {
  absl::Status creation_status;
  TypedRendezvousHashLbConfig typed_config(config_, context_.regex_engine_, creation_status);
  ASSERT(creation_status.ok());

  lb_ = std::make_unique<RendezvousHashLoadBalancer>(
      priority_set_, stats_, *stats_store_.rootScope(), context_.runtime_loader_,
      context_.api_.random_, 50, typed_config.lb_config_, typed_config.hash_policy_);
  EXPECT_EQ(nullptr, lb_->factory()->create(lb_params_)->chooseHost(nullptr).host);
}

// Given no hosts, expect chooseHost to return null.
TEST_P(RendezvousHashLoadBalancerTest, NoHost) {
  init();
  EXPECT_EQ(nullptr, lb_->factory()->create(lb_params_)->chooseHost(nullptr).host);

  EXPECT_EQ(nullptr, lb_->factory()->create(lb_params_)->peekAnotherHost(nullptr));
  EXPECT_FALSE(lb_->factory()->create(lb_params_)->lifetimeCallbacks().has_value());
  std::vector<uint8_t> hash_key;
  auto mock_host = std::make_shared<NiceMock<MockHost>>();
  EXPECT_FALSE(lb_->factory()
                   ->create(lb_params_)
                   ->selectExistingConnection(nullptr, *mock_host, hash_key)
                   .has_value());
}

TEST_P(RendezvousHashLoadBalancerTest, BaseMethods) {
  init();
  EXPECT_EQ(nullptr, lb_->peekAnotherHost(nullptr));
  EXPECT_FALSE(lb_->lifetimeCallbacks().has_value());
  std::vector<uint8_t> hash_key;
  auto mock_host = std::make_shared<NiceMock<MockHost>>();
  EXPECT_FALSE(lb_->selectExistingConnection(nullptr, *mock_host, hash_key).has_value());
};

// Test for thread aware load balancer destructed before load balancer factory.
TEST_P(RendezvousHashLoadBalancerTest, LbDestructedBeforeFactory) {
  init();

  auto factory = lb_->factory();
  lb_.reset();

  EXPECT_NE(nullptr, factory->create(lb_params_));
}

// Basic test with 6 hosts - verify consistent hashing behavior.
TEST_P(RendezvousHashLoadBalancerTest, Basic) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93"),
      makeTestHost(info_, "tcp://127.0.0.1:94"), makeTestHost(info_, "tcp://127.0.0.1:95")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  init();

  LoadBalancerPtr lb = lb_->factory()->create(lb_params_);

  // Test that the same hash key always returns the same host
  {
    TestLoadBalancerContext context(12345);
    auto host1 = lb->chooseHost(&context).host;
    auto host2 = lb->chooseHost(&context).host;
    EXPECT_EQ(host1, host2);
  }

  // Test different hash keys can return different hosts
  {
    TestLoadBalancerContext context1(100);
    TestLoadBalancerContext context2(200);
    TestLoadBalancerContext context3(300);
    auto host1 = lb->chooseHost(&context1).host;
    auto host2 = lb->chooseHost(&context2).host;
    auto host3 = lb->chooseHost(&context3).host;
    // At least verify hosts are not null
    EXPECT_NE(nullptr, host1);
    EXPECT_NE(nullptr, host2);
    EXPECT_NE(nullptr, host3);
  }

  // Test random hash when no context provided
  {
    EXPECT_CALL(context_.api_.random_, random()).WillOnce(Return(12345UL));
    auto host = lb->chooseHost(nullptr).host;
    EXPECT_NE(nullptr, host);
  }

  EXPECT_EQ(0UL, stats_.lb_healthy_panic_.value());

  // Test panic mode
  hostSet().healthy_hosts_.clear();
  hostSet().runCallbacks({}, {});
  lb = lb_->factory()->create(lb_params_);
  {
    TestLoadBalancerContext context(0);
    EXPECT_NE(nullptr, lb->chooseHost(&context).host);
  }
  EXPECT_EQ(1UL, stats_.lb_healthy_panic_.value());
}

// Ensure if all the hosts with priority 0 unhealthy, the next priority hosts are used.
TEST_P(RendezvousHashFailoverTest, BasicFailover) {
  host_set_.hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80")};
  failover_host_set_.healthy_hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:82")};
  failover_host_set_.hosts_ = failover_host_set_.healthy_hosts_;

  init();

  LoadBalancerPtr lb = lb_->factory()->create(lb_params_);
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb->chooseHost(nullptr).host);

  // Add a healthy host at P=0 and it will be chosen.
  host_set_.healthy_hosts_ = host_set_.hosts_;
  host_set_.runCallbacks({}, {});
  lb = lb_->factory()->create(lb_params_);
  EXPECT_EQ(host_set_.healthy_hosts_[0], lb->chooseHost(nullptr).host);

  // Remove the healthy host and ensure we fail back over to the failover_host_set_
  host_set_.healthy_hosts_ = {};
  host_set_.runCallbacks({}, {});
  lb = lb_->factory()->create(lb_params_);
  EXPECT_EQ(failover_host_set_.healthy_hosts_[0], lb->chooseHost(nullptr).host);
}

// Test bounded load configuration.
TEST_P(RendezvousHashLoadBalancerTest, BasicWithBoundedLoad) {
  hostSet().hosts_ = {makeTestHost(info_, "90", "tcp://127.0.0.1:90"),
                      makeTestHost(info_, "91", "tcp://127.0.0.1:91"),
                      makeTestHost(info_, "92", "tcp://127.0.0.1:92"),
                      makeTestHost(info_, "93", "tcp://127.0.0.1:93"),
                      makeTestHost(info_, "94", "tcp://127.0.0.1:94"),
                      makeTestHost(info_, "95", "tcp://127.0.0.1:95")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_.mutable_consistent_hashing_lb_config()->set_use_hostname_for_hashing(true);
  config_.mutable_consistent_hashing_lb_config()->mutable_hash_balance_factor()->set_value(200);

  init();

  LoadBalancerPtr lb = lb_->factory()->create(lb_params_);
  {
    TestLoadBalancerContext context(0);
    EXPECT_NE(nullptr, lb->chooseHost(&context).host);
  }
  EXPECT_EQ(0UL, stats_.lb_healthy_panic_.value());
}

// Expect reasonable results with hostname.
TEST_P(RendezvousHashLoadBalancerTest, BasicWithHostname) {
  hostSet().hosts_ = {makeTestHost(info_, "90", "tcp://127.0.0.1:90"),
                      makeTestHost(info_, "91", "tcp://127.0.0.1:91"),
                      makeTestHost(info_, "92", "tcp://127.0.0.1:92"),
                      makeTestHost(info_, "93", "tcp://127.0.0.1:93"),
                      makeTestHost(info_, "94", "tcp://127.0.0.1:94"),
                      makeTestHost(info_, "95", "tcp://127.0.0.1:95")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  config_.mutable_consistent_hashing_lb_config()->set_use_hostname_for_hashing(true);

  init();

  LoadBalancerPtr lb = lb_->factory()->create(lb_params_);

  // Test consistent hashing with hostname
  {
    TestLoadBalancerContext context(12345);
    auto host1 = lb->chooseHost(&context).host;
    auto host2 = lb->chooseHost(&context).host;
    EXPECT_EQ(host1, host2);
  }

  EXPECT_EQ(0UL, stats_.lb_healthy_panic_.value());
}

// Test retry host predicate behavior.
TEST_P(RendezvousHashLoadBalancerTest, BasicWithRetryHostPredicate) {
  hostSet().hosts_ = {
      makeTestHost(info_, "tcp://127.0.0.1:90"), makeTestHost(info_, "tcp://127.0.0.1:91"),
      makeTestHost(info_, "tcp://127.0.0.1:92"), makeTestHost(info_, "tcp://127.0.0.1:93"),
      makeTestHost(info_, "tcp://127.0.0.1:94"), makeTestHost(info_, "tcp://127.0.0.1:95")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  init();

  LoadBalancerPtr lb = lb_->factory()->create(lb_params_);
  {
    // First attempt succeeds even when retry count is > 0.
    TestLoadBalancerContext context(12345, 2, [](const Host&) { return false; });
    EXPECT_NE(nullptr, lb->chooseHost(&context).host);
  }
  {
    // Different attempts should potentially return different hosts
    TestLoadBalancerContext context1(12345);
    auto host1 = lb->chooseHost(&context1).host;

    // With retry, the hash gets mutated, potentially returning a different host
    TestLoadBalancerContext context2(12345, 1,
                                     [&](const Host& host) { return &host == host1.get(); });
    auto host2 = lb->chooseHost(&context2).host;
    // We can't guarantee a different host, but we can verify we got a valid host
    EXPECT_NE(nullptr, host2);
  }
}

// Given hosts with weights 1, 2 and 3, and a sufficiently large number of requests,
// expect that requests will distribute to the hosts with approximately the right proportion.
TEST_P(RendezvousHashLoadBalancerTest, HostWeightedDistribution) {
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90", 1),
                      makeTestHost(info_, "tcp://127.0.0.1:91", 2),
                      makeTestHost(info_, "tcp://127.0.0.1:92", 3)};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  init();
  LoadBalancerPtr lb = lb_->factory()->create(lb_params_);

  // Generate many hashes and populate a histogram of which hosts they mapped to...
  uint32_t counts[3] = {0};
  for (uint32_t i = 0; i < 6000; ++i) {
    TestLoadBalancerContext context(i * (std::numeric_limits<uint64_t>::max() / 6000));
    uint32_t port = lb->chooseHost(&context).host->address()->ip()->port();
    ++counts[port - 90];
  }

  // Rendezvous hashing with weights should give approximately 1:2:3 ratio.
  // Allow some tolerance due to the nature of hashing.
  // Expected: ~1000, ~2000, ~3000
  EXPECT_GT(counts[0], 500);  // :90 should get some traffic
  EXPECT_LT(counts[0], 1500); // but not too much
  EXPECT_GT(counts[1], 1500); // :91 should get more traffic
  EXPECT_LT(counts[1], 2500);
  EXPECT_GT(counts[2], 2500); // :92 should get the most traffic
  EXPECT_LT(counts[2], 3500);
}

// Given locality weights, and a sufficiently large number of requests, expect that requests
// will distribute to the hosts with approximately the right proportion.
TEST_P(RendezvousHashLoadBalancerTest, LocalityWeightedDistribution) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  envoy::config::core::v3::Locality zone_c;
  zone_c.set_zone("C");
  envoy::config::core::v3::Locality zone_d;
  zone_d.set_zone("D");

  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90", zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:91", zone_b),
                      makeTestHost(info_, "tcp://127.0.0.1:92", zone_c),
                      makeTestHost(info_, "tcp://127.0.0.1:93", zone_d)};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().hosts_per_locality_ = makeHostsPerLocality(
      {{hostSet().hosts_[0]}, {hostSet().hosts_[1]}, {hostSet().hosts_[2]}, {hostSet().hosts_[3]}});
  hostSet().healthy_hosts_per_locality_ = hostSet().hosts_per_locality_;
  hostSet().locality_weights_ = makeLocalityWeights({1, 2, 3, 0});
  hostSet().runCallbacks({}, {});

  init(true);
  LoadBalancerPtr lb = lb_->factory()->create(lb_params_);

  // Generate many hashes and populate a histogram of which hosts they mapped to...
  uint32_t counts[4] = {0};
  for (uint32_t i = 0; i < 6000; ++i) {
    TestLoadBalancerContext context(i * (std::numeric_limits<uint64_t>::max() / 6000));
    uint32_t port = lb->chooseHost(&context).host->address()->ip()->port();
    ++counts[port - 90];
  }

  // With locality weights 1:2:3:0, expect approximately that ratio
  EXPECT_GT(counts[0], 500); // :90 should get some traffic
  EXPECT_LT(counts[0], 1500);
  EXPECT_GT(counts[1], 1500); // :91 should get more traffic
  EXPECT_LT(counts[1], 2500);
  EXPECT_GT(counts[2], 2500); // :92 should get the most traffic
  EXPECT_LT(counts[2], 3500);
  EXPECT_EQ(0, counts[3]); // :93 should get no traffic (weight 0)
}

// Test consistency - adding/removing hosts should only affect keys mapped to those hosts.
TEST_P(RendezvousHashLoadBalancerTest, ConsistencyOnHostChange) {
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90"),
                      makeTestHost(info_, "tcp://127.0.0.1:91"),
                      makeTestHost(info_, "tcp://127.0.0.1:92")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  init();
  LoadBalancerPtr lb = lb_->factory()->create(lb_params_);

  // Record host mappings for several keys
  std::vector<std::pair<uint64_t, HostConstSharedPtr>> original_mappings;
  for (uint32_t i = 0; i < 100; ++i) {
    uint64_t key = i * 1000000;
    TestLoadBalancerContext context(key);
    original_mappings.emplace_back(key, lb->chooseHost(&context).host);
  }

  // Add a new host
  hostSet().hosts_.push_back(makeTestHost(info_, "tcp://127.0.0.1:93"));
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  lb = lb_->factory()->create(lb_params_);

  // Count how many keys changed their mapping
  uint32_t changed_count = 0;
  for (const auto& mapping : original_mappings) {
    TestLoadBalancerContext context(mapping.first);
    auto new_host = lb->chooseHost(&context).host;
    if (new_host != mapping.second) {
      ++changed_count;
    }
  }

  // With rendezvous hashing, adding one host out of 4 should affect roughly 1/4 of keys
  // We allow some tolerance here
  EXPECT_LT(changed_count, 50); // Should be around 25, definitely less than half
}

// Test that two hosts are selected differently for the same key
TEST_P(RendezvousHashLoadBalancerTest, TwoHostsBasic) {
  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:80"),
                      makeTestHost(info_, "tcp://127.0.0.1:81")};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().runCallbacks({}, {});

  init();
  LoadBalancerPtr lb = lb_->factory()->create(lb_params_);

  // Test that both hosts can be selected with different keys
  bool selected_host_0 = false;
  bool selected_host_1 = false;
  for (uint32_t i = 0; i < 100 && !(selected_host_0 && selected_host_1); ++i) {
    TestLoadBalancerContext context(i * 12345);
    auto host = lb->chooseHost(&context).host;
    if (host == hostSet().hosts_[0]) {
      selected_host_0 = true;
    } else if (host == hostSet().hosts_[1]) {
      selected_host_1 = true;
    }
  }
  EXPECT_TRUE(selected_host_0);
  EXPECT_TRUE(selected_host_1);
}

// Zero locality weights should result in no hosts.
TEST_P(RendezvousHashLoadBalancerTest, ZeroLocalityWeights) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");

  hostSet().hosts_ = {makeTestHost(info_, "tcp://127.0.0.1:90", zone_a),
                      makeTestHost(info_, "tcp://127.0.0.1:91", zone_b)};
  hostSet().healthy_hosts_ = hostSet().hosts_;
  hostSet().hosts_per_locality_ =
      makeHostsPerLocality({{hostSet().hosts_[0]}, {hostSet().hosts_[1]}});
  hostSet().healthy_hosts_per_locality_ = hostSet().hosts_per_locality_;
  hostSet().locality_weights_ = makeLocalityWeights({0, 0});
  hostSet().runCallbacks({}, {});

  init(true);
  EXPECT_EQ(nullptr, lb_->factory()->create(lb_params_)->chooseHost(nullptr).host);
}

} // namespace
} // namespace Upstream
} // namespace Envoy
