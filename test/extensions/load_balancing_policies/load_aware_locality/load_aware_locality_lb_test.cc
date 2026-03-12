#include <chrono>
#include <cstdint>
#include <limits>

#include "source/extensions/load_balancing_policies/load_aware_locality/config.h"
#include "source/extensions/load_balancing_policies/load_aware_locality/load_aware_locality_lb.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/mocks/upstream/typed_load_balancer_factory.h"

#include "absl/container/flat_hash_map.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {
namespace {

// Helper to create a MockHost that tracks weight and orcaUtilization state.
std::shared_ptr<NiceMock<Upstream::MockHost>>
makeWeightTrackingMockHost(uint32_t initial_weight = 1) {
  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto weight = std::make_shared<uint32_t>(initial_weight);
  ON_CALL(*host, weight()).WillByDefault([weight]() -> uint32_t { return *weight; });
  ON_CALL(*host, weight(::testing::_)).WillByDefault([weight](uint32_t new_weight) {
    *weight = new_weight;
  });
  // orca_utilization_store_ initializes to 0.0 (no data) by default.
  return host;
}

class LoadAwareLocalityLbTest : public testing::Test {
protected:
  void SetUp() override {
    ON_CALL(context_, mainThreadDispatcher()).WillByDefault(ReturnRef(dispatcher_));
    // Set up mock random generator to return values distributed across the full uint64_t range.
    // selectLocality() divides random() by max() to get a [0,1) ratio, so we need values
    // that span the range to actually exercise different locality selections.
    random_call_count_ = 0;
    ON_CALL(random_, random()).WillByDefault([this]() -> uint64_t {
      constexpr uint64_t step = std::numeric_limits<uint64_t>::max() / 100;
      return (random_call_count_++ % 100) * step;
    });
    // Capture the timer created by LoadAwareLocalityLoadBalancer.
    timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);
  }

  // Create the LB with given config and verify it initializes successfully.
  void createLb(double variance_threshold = 0.1, double ewma_alpha = 1.0,
                double probe_percentage = 0.0,
                std::chrono::milliseconds weight_expiration_period = std::chrono::milliseconds(0)) {
    auto weight_update_period = std::chrono::milliseconds(1000);

    // Resolve the round robin factory.
    envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin rr_config_msg;
    envoy::config::core::v3::TypedExtensionConfig rr_config;
    rr_config.set_name("envoy.load_balancing_policies.round_robin");
    rr_config.mutable_typed_config()->PackFrom(rr_config_msg);

    auto& rr_factory =
        Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(rr_config);

    auto rr_proto = rr_factory.createEmptyConfigProto();
    ASSERT_TRUE(Config::Utility::translateOpaqueConfig(
                    rr_config.typed_config(), context_.messageValidationVisitor(), *rr_proto)
                    .ok());
    auto rr_lb_config = rr_factory.loadConfig(context_, *rr_proto).value();

    auto lb_config = std::make_unique<LoadAwareLocalityLbConfig>(
        rr_factory, rr_factory.name(), std::move(rr_lb_config), weight_update_period,
        variance_threshold, ewma_alpha, probe_percentage, weight_expiration_period, dispatcher_,
        context_.thread_local_);

    thread_aware_lb_ = std::make_unique<LoadAwareLocalityLoadBalancer>(
        *lb_config, cluster_info_, priority_set_, context_.runtime_loader_, random_,
        context_.time_system_);

    ASSERT_TRUE(thread_aware_lb_->initialize().ok());
    factory_ = thread_aware_lb_->factory();
  }

  // Set up hosts across any number of localities on the given priority.
  // When healthy_localities is provided, it overrides the healthy hosts per locality
  // (otherwise healthy == all).
  void setupPriorityLocalities(
      uint32_t priority, std::vector<Upstream::HostVector> localities,
      bool has_local_locality = false,
      absl::optional<std::vector<Upstream::HostVector>> healthy_localities = absl::nullopt,
      absl::optional<std::vector<Upstream::HostVector>> degraded_localities = absl::nullopt) {
    auto* host_set = priority_set_.getMockHostSet(priority);
    Upstream::HostVector all_hosts;
    for (const auto& lv : localities) {
      all_hosts.insert(all_hosts.end(), lv.begin(), lv.end());
    }
    host_set->hosts_ = all_hosts;
    host_set->hosts_per_locality_ =
        Upstream::makeHostsPerLocality(std::move(localities), !has_local_locality);

    if (healthy_localities.has_value()) {
      Upstream::HostVector healthy_hosts;
      for (const auto& lv : *healthy_localities) {
        healthy_hosts.insert(healthy_hosts.end(), lv.begin(), lv.end());
      }
      host_set->healthy_hosts_ = healthy_hosts;
      host_set->healthy_hosts_per_locality_ =
          Upstream::makeHostsPerLocality(std::move(*healthy_localities), !has_local_locality);
    } else {
      host_set->healthy_hosts_ = all_hosts;
      host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;
    }

    if (degraded_localities.has_value()) {
      Upstream::HostVector degraded_hosts;
      for (const auto& lv : *degraded_localities) {
        degraded_hosts.insert(degraded_hosts.end(), lv.begin(), lv.end());
      }
      host_set->degraded_hosts_ = degraded_hosts;
      host_set->degraded_hosts_per_locality_ =
          Upstream::makeHostsPerLocality(std::move(*degraded_localities), !has_local_locality);
    } else {
      host_set->degraded_hosts_.clear();
      host_set->degraded_hosts_per_locality_ =
          Upstream::makeHostsPerLocality({}, !has_local_locality);
    }
  }

  void setupLocalities(
      std::vector<Upstream::HostVector> localities, bool has_local_locality = false,
      absl::optional<std::vector<Upstream::HostVector>> healthy_localities = absl::nullopt,
      absl::optional<std::vector<Upstream::HostVector>> degraded_localities = absl::nullopt) {
    setupPriorityLocalities(0, std::move(localities), has_local_locality,
                            std::move(healthy_localities), std::move(degraded_localities));
  }

  // Set ORCA utilization on a host.
  void setHostUtilization(Upstream::MockHost& host, double utilization) {
    host.orca_utilization_store_.set(utilization, nowMs());
  }

  // Set ORCA utilization with an explicit monotonic timestamp (ms).
  void setHostUtilizationWithTime(Upstream::MockHost& host, double utilization,
                                  int64_t monotonic_time_ms) {
    host.orca_utilization_store_.set(utilization, monotonic_time_ms);
  }

  // Clear ORCA data by writing sentinel values, simulating "never reported".
  // Resets value to 0.0 and lastUpdateTimeMs to 0.
  void clearHostUtilization(Upstream::MockHost& host) { host.orca_utilization_store_.set(0.0, 0); }

  // Return current monotonic time in ms from the test's TimeSource.
  int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               context_.time_system_.monotonicTime().time_since_epoch())
        .count();
  }

  // Count how many times each host is selected over num_picks.
  absl::flat_hash_map<const Upstream::Host*, int> countPicks(Upstream::LoadBalancer& lb,
                                                             int num_picks) {
    absl::flat_hash_map<const Upstream::Host*, int> counts;
    for (int i = 0; i < num_picks; ++i) {
      auto result = lb.chooseHost(nullptr);
      EXPECT_NE(nullptr, result.host);
      if (result.host) {
        counts[result.host.get()]++;
      }
    }
    return counts;
  }

  // Check whether a host is selected at least once over num_picks.
  bool hostSeen(Upstream::LoadBalancer& lb, const Upstream::HostConstSharedPtr& target,
                int picks = 200) {
    for (int i = 0; i < picks; ++i) {
      auto result = lb.chooseHost(nullptr);
      if (result.host == target) {
        return true;
      }
    }
    return false;
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterInfo> cluster_info_;
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  NiceMock<Random::MockRandomGenerator> random_;
  uint64_t random_call_count_{0};
  Event::MockTimer* timer_{};

  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
  Upstream::LoadBalancerFactorySharedPtr factory_;
};

// Test: Worker LB can be created from empty priority set.
TEST_F(LoadAwareLocalityLbTest, EmptyPrioritySet) {
  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  auto result = worker_lb->chooseHost(nullptr);
  EXPECT_EQ(nullptr, result.host);
}

// Test: Worker LB creates per-locality child LBs and selects hosts.
TEST_F(LoadAwareLocalityLbTest, SingleLocalitySelectsHost) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1, h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // With a single locality, should always select from it.
  auto result = worker_lb->chooseHost(nullptr);
  EXPECT_NE(nullptr, result.host);
}

// Test: With two localities and no ORCA data, traffic is split across localities.
TEST_F(LoadAwareLocalityLbTest, TwoLocalitiesNoOrcaData) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Both localities have weight = 1 * 1.0 = 1.0 (no utilization data), so roughly 50/50 split.
  const int num_picks = 1000;
  auto counts = countPicks(*worker_lb, num_picks);
  EXPECT_GT(counts[h1.get()], num_picks * 0.3);
  EXPECT_GT(counts[h2.get()], num_picks * 0.3);
}

// Test: Utilization affects routing weights - high utilization locality gets less traffic.
TEST_F(LoadAwareLocalityLbTest, UtilizationAffectsRouting) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  // ewma_alpha=1.0 means no smoothing (raw values used directly).
  createLb();

  // Now set utilization on hosts. h1 is high utilization (0.9), h2 is low (0.1).
  setHostUtilization(*h1, 0.9);
  setHostUtilization(*h2, 0.1);

  // Trigger locality weight recomputing via re-initialize.
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // h1 locality has weight = 1*0.1 = 0.1, h2 has weight = 1*0.9 = 0.9.
  // Expect most traffic to h2.
  const int num_picks = 2000;
  auto counts = countPicks(*worker_lb, num_picks);

  // Locality B (low util = 0.1, weight = 0.9) should get substantially more traffic.
  EXPECT_GT(counts[h2.get()], counts[h1.get()]);
  EXPECT_GT(counts[h2.get()], num_picks * 0.7);
}

// Test: Local zone preference when variance is below threshold.
// With probe_percentage=0, 100% goes to local. Verifies both snapshot weights
// (weights[0]=1, weights[1]=0) and actual host selection (all picks → h1).
TEST_F(LoadAwareLocalityLbTest, LocalZonePreference) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  // has_local_locality = true means locality[0] is the local one.
  setupLocalities({{h1}, {h2}}, /*has_local_locality=*/true);

  // Set variance threshold very high so that all_local is always triggered.
  // probe_percentage=0 to disable probing.
  createLb(/*variance_threshold=*/1.0, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);

  // Set similar utilization on both hosts (within threshold).
  setHostUtilization(*h1, 0.5);
  setHostUtilization(*h2, 0.5);

  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  // Verify snapshot weights: 100% local, 0% remote.
  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  ASSERT_NE(nullptr, typed_factory);
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  EXPECT_TRUE(weights->all_local);
  EXPECT_NEAR(weights->weights[0], 1.0, 0.01);
  EXPECT_NEAR(weights->weights[1], 0.0, 0.01);

  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // With all_local = true and probe=0, all traffic should go to locality 0 (h1).
  const int num_picks = 100;
  for (int i = 0; i < num_picks; ++i) {
    auto result = worker_lb->chooseHost(nullptr);
    ASSERT_NE(nullptr, result.host);
    EXPECT_EQ(result.host, h1);
  }
}

// Test: Three localities with varying utilization.
TEST_F(LoadAwareLocalityLbTest, ThreeLocalitiesWeightedDistribution) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  auto h3 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}, {h3}});

  createLb();

  // Set utilization: h1=0.8 (weight=0.2), h2=0.5 (weight=0.5), h3=0.2 (weight=0.8).
  setHostUtilization(*h1, 0.8);
  setHostUtilization(*h2, 0.5);
  setHostUtilization(*h3, 0.2);

  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Total weight = 0.2 + 0.5 + 0.8 = 1.5
  // Expected: h1 ~13%, h2 ~33%, h3 ~53%
  auto counts = countPicks(*worker_lb, 3000);
  EXPECT_GT(counts[h3.get()], counts[h2.get()]);
  EXPECT_GT(counts[h2.get()], counts[h1.get()]);
}

// Test: RoutingWeightsSnapshot is correctly computed.
TEST_F(LoadAwareLocalityLbTest, RoutingWeightsComputed) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();

  // Set utilization and re-initialize to trigger recomputing.
  setHostUtilization(*h1, 0.7);
  setHostUtilization(*h2, 0.3);

  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  // Check the factory's routing weights were published correctly.
  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  ASSERT_NE(nullptr, typed_factory);
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  // With 1 host per locality: weight = 1 * (1.0 - util)
  ASSERT_EQ(2, weights->weights.size());
  EXPECT_NEAR(weights->weights[0], 0.3, 0.01); // 1 * (1.0 - 0.7)
  EXPECT_NEAR(weights->weights[1], 0.7, 0.01); // 1 * (1.0 - 0.3)
  EXPECT_NEAR(weights->total_weight, 1.0, 0.02);
  EXPECT_FALSE(weights->all_local);
}

// Test: Stub LB methods return expected defaults.
TEST_F(LoadAwareLocalityLbTest, StubMethodsReturnDefaults) {
  auto h1 = makeWeightTrackingMockHost();
  setupLocalities({{h1}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  EXPECT_FALSE(worker_lb->lifetimeCallbacks().has_value());
  std::vector<uint8_t> hash_key;
  EXPECT_FALSE(worker_lb->selectExistingConnection(nullptr, *h1, hash_key).has_value());
}

// --- New tests for host-count weighting, EWMA, and probe ---

// Test: Host-count asymmetry — locality with more hosts gets proportionally more traffic.
TEST_F(LoadAwareLocalityLbTest, HostCountAsymmetry) {
  // Locality A: 10 hosts, Locality B: 2 hosts, both at 50% utilization.
  Upstream::HostVector locality_a_hosts, locality_b_hosts;
  for (int i = 0; i < 10; ++i) {
    locality_a_hosts.push_back(makeWeightTrackingMockHost());
  }
  for (int i = 0; i < 2; ++i) {
    locality_b_hosts.push_back(makeWeightTrackingMockHost());
  }
  setupLocalities({locality_a_hosts, locality_b_hosts});

  createLb();

  for (auto& host : locality_a_hosts) {
    setHostUtilization(dynamic_cast<Upstream::MockHost&>(*host), 0.5);
  }
  for (auto& host : locality_b_hosts) {
    setHostUtilization(dynamic_cast<Upstream::MockHost&>(*host), 0.5);
  }

  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  // weight[A] = 10 * 0.5 = 5.0, weight[B] = 2 * 0.5 = 1.0
  EXPECT_NEAR(weights->weights[0], 5.0, 0.01);
  EXPECT_NEAR(weights->weights[1], 1.0, 0.01);
  // Locality A should get ~5x the traffic.
  EXPECT_NEAR(weights->weights[0] / weights->weights[1], 5.0, 0.1);
}

// Test: EWMA smoothing — spike doesn't cause full jump.
TEST_F(LoadAwareLocalityLbTest, EwmaSmoothing) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  // ewma_alpha=0.3 for smoothing.
  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/0.3);

  // First tick: set utilization at 0.5 for both. This is cold start, raw values used.
  setHostUtilization(*h1, 0.5);
  setHostUtilization(*h2, 0.5);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights1 = typed_factory->routingWeights();
  // Cold start: smoothed = raw = 0.5 for both.
  EXPECT_NEAR(weights1->weights[0], 0.5, 0.01); // 1 * (1.0 - 0.5)
  EXPECT_NEAR(weights1->weights[1], 0.5, 0.01);

  // Second tick: h1 spikes to 1.0. With alpha=0.3:
  // smoothed[0] = 0.3 * 1.0 + 0.7 * 0.5 = 0.65 (not 1.0)
  setHostUtilization(*h1, 1.0);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto weights2 = typed_factory->routingWeights();
  // weight[0] = 1 * max(0, 1.0 - 0.65) = 0.35
  // weight[1] = 1 * max(0, 1.0 - 0.5) = 0.5 (h2 unchanged)
  EXPECT_NEAR(weights2->weights[0], 0.35, 0.05);
  EXPECT_NEAR(weights2->weights[1], 0.5, 0.05);
  // Without smoothing, weight[0] would be 0. With smoothing, it's still > 0.
  EXPECT_GT(weights2->weights[0], 0.0);
}

// Test: Expired ORCA samples clear the EWMA state and fall back to "no data".
TEST_F(LoadAwareLocalityLbTest, EwmaExpiredDataClearsState) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/0.3, /*probe_percentage=*/0.0,
           /*weight_expiration_period=*/std::chrono::milliseconds(5000));

  // First tick: both at 0.8.
  setHostUtilization(*h1, 0.8);
  setHostUtilization(*h2, 0.8);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  // Once the sample expires, the stale EWMA should be dropped instead of retained indefinitely.
  context_.time_system_.advanceTimeWait(std::chrono::milliseconds(6000));
  clearHostUtilization(*h2);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  // h2 has no fresh data anymore, so its stale 0.8 EWMA must be cleared.
  EXPECT_NEAR(weights->weights[1], 1.0, 0.01);
}

// Test: A host reporting 0.0 utilization (fully idle) is treated as valid data, not "no data".
// This verifies the fix for the 0.0-vs-no-data ambiguity: lastUpdateTimeMs() > 0 means the
// host has reported, even if the utilization value is 0.0.
TEST_F(LoadAwareLocalityLbTest, ZeroUtilizationIsValidData) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();

  // h1 reports 0.0 utilization (fully idle), h2 reports 0.9 (heavily loaded).
  setHostUtilization(*h1, 0.0);
  setHostUtilization(*h2, 0.9);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);

  // h1: weight = 1 * (1.0 - 0.0) = 1.0 (0.0 is valid data, not skipped)
  // h2: weight = 1 * (1.0 - 0.9) = 0.1
  EXPECT_NEAR(weights->weights[0], 1.0, 0.01);
  EXPECT_NEAR(weights->weights[1], 0.1, 0.01);

  // h1 should get the vast majority of traffic.
  auto worker_lb = factory_->create({priority_set_, nullptr});
  auto counts = countPicks(*worker_lb, 1000);
  EXPECT_GT(counts[h1.get()], 800);
}

// Test: Probe in all_local mode — remote gets ~3% even when all_local triggers.
TEST_F(LoadAwareLocalityLbTest, ProbeInAllLocalMode) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}}, /*has_local_locality=*/true);

  // High variance threshold to trigger all_local, 3% probe.
  createLb(/*variance_threshold=*/1.0, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.03);

  setHostUtilization(*h1, 0.5);
  setHostUtilization(*h2, 0.5);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  EXPECT_TRUE(weights->all_local);
  // Local should be ~97%, remote ~3%.
  EXPECT_NEAR(weights->weights[0], 0.97, 0.01);
  EXPECT_NEAR(weights->weights[1], 0.03, 0.01);
}

// Test: Probe with zero-weight remotes — overloaded remote still gets probe share.
TEST_F(LoadAwareLocalityLbTest, ProbeWithZeroWeightRemotes) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}}, /*has_local_locality=*/true);

  // all_local triggers, then probe redistributes.
  createLb(/*variance_threshold=*/1.0, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.05);

  setHostUtilization(*h1, 0.3);
  setHostUtilization(*h2, 1.0); // Fully saturated remote
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  // Even though remote has 0 headroom, probe ensures it gets 5%.
  EXPECT_TRUE(weights->all_local);
  EXPECT_NEAR(weights->weights[1], 0.05, 0.01);
  EXPECT_GT(weights->weights[1], 0.0);
}

// Test: Variance threshold uses target_util (host-count-weighted remote average).
TEST_F(LoadAwareLocalityLbTest, VarianceThresholdUsesTargetUtil) {
  // Local: 10 hosts at 0.4 util, Remote: 2 hosts at 0.6 util.
  // target_util = (0.6*2) / 2 = 0.6  (remote-only average)
  // local_util (0.4) <= target_util (0.6) + threshold (0.1) = 0.7 → all_local = true
  Upstream::HostVector locality_a_hosts, locality_b_hosts;
  for (int i = 0; i < 10; ++i) {
    locality_a_hosts.push_back(makeWeightTrackingMockHost());
  }
  for (int i = 0; i < 2; ++i) {
    locality_b_hosts.push_back(makeWeightTrackingMockHost());
  }

  setupLocalities({locality_a_hosts, locality_b_hosts}, /*has_local_locality=*/true);

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);

  for (auto& host : locality_a_hosts) {
    setHostUtilization(dynamic_cast<Upstream::MockHost&>(*host), 0.4);
  }
  for (auto& host : locality_b_hosts) {
    setHostUtilization(dynamic_cast<Upstream::MockHost&>(*host), 0.6);
  }
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  EXPECT_TRUE(weights->all_local);
}

// Test: All localities saturated (util=1.0) → fallback distributes proportionally by host count.
// With equal host counts the fallback gives each locality equal weight (no single locality
// favored).
TEST_F(LoadAwareLocalityLbTest, AllLocalitiesSaturated) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();

  setHostUtilization(*h1, 1.0);
  setHostUtilization(*h2, 1.0);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  // Verify the fallback: weights set proportional to host count (1:1 → [1.0, 1.0]).
  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  EXPECT_NEAR(weights->weights[0], 1.0, 0.01);
  EXPECT_NEAR(weights->weights[1], 1.0, 0.01);
  EXPECT_NEAR(weights->total_weight, 2.0, 0.01);

  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Both localities get traffic (roughly 50/50 with equal host counts).
  EXPECT_TRUE(hostSeen(*worker_lb, h1));
  EXPECT_TRUE(hostSeen(*worker_lb, h2));
}

// --- Tests for incremental host updates (recreateOnHostChange = false) ---

// Test: Adding a host to one locality is picked up incrementally without rebuilding all localities.
TEST_F(LoadAwareLocalityLbTest, IncrementalHostAdd) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Verify both hosts are reachable.
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 100));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 100));

  // Add h3 to locality B (same as h2).
  auto h3 = makeWeightTrackingMockHost();
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {h1, h2, h3};
  host_set->healthy_hosts_ = {h1, h2, h3};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{h1}, {h2, h3}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  // Fire the callback to trigger incremental update.
  priority_set_.runUpdateCallbacks(0, {h3}, {});

  // Verify h3 is now reachable.
  EXPECT_TRUE(hostSeen(*worker_lb, h3));
}

// Test: Removing a host from a locality means it's no longer selected.
TEST_F(LoadAwareLocalityLbTest, IncrementalHostRemove) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  auto h3 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2, h3}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});

  // Now remove h3 from locality B.
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {h1, h2};
  host_set->healthy_hosts_ = {h1, h2};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{h1}, {h2}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  priority_set_.runUpdateCallbacks(0, {}, {h3});

  // Verify h3 is never selected.
  EXPECT_FALSE(hostSeen(*worker_lb, h3));
}

// Test: Adding a new locality (topology change) triggers a full rebuild.
TEST_F(LoadAwareLocalityLbTest, TopologyChangeTriggersRebuild) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});

  // Add a third locality with h3.
  auto h3 = makeWeightTrackingMockHost();
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {h1, h2, h3};
  host_set->healthy_hosts_ = {h1, h2, h3};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{h1}, {h2}, {h3}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  priority_set_.runUpdateCallbacks(0, {h3}, {});

  // Update routing weights to reflect the new 3-locality topology.
  auto snapshot = std::make_shared<RoutingWeightsSnapshot>();
  snapshot->weights = {1.0, 1.0, 1.0};
  snapshot->total_weight = 3.0;
  snapshot->priority_weights = {{{1.0, 1.0, 1.0}, 3.0, false, false}};
  snapshot->priority_loads.healthy_priority_load_.get() = {100};
  snapshot->priority_loads.degraded_priority_load_.get() = {0};
  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  typed_factory->updateRoutingWeights(std::move(snapshot));

  // Verify h3 is reachable (full rebuild happened).
  EXPECT_TRUE(hostSeen(*worker_lb, h3, 300));
}

// Test: Routing weights are refreshed on each chooseHost call (no longer cached at construction).
TEST_F(LoadAwareLocalityLbTest, RoutingWeightsRefreshOnChooseHost) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});

  // Initially no ORCA data, both localities get equal weight. Verify both are hit.
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 100));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 100));

  // Now update routing weights via the factory to heavily favor locality B.
  auto snapshot = std::make_shared<RoutingWeightsSnapshot>();
  snapshot->weights = {0.0, 1.0};
  snapshot->total_weight = 1.0;
  snapshot->priority_weights = {{{0.0, 1.0}, 1.0, false, false}};
  snapshot->priority_loads.healthy_priority_load_.get() = {100};
  snapshot->priority_loads.degraded_priority_load_.get() = {0};
  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  typed_factory->updateRoutingWeights(std::move(snapshot));

  // The same worker_lb (not recreated) should now use the new weights.
  // With weight[0]=0 and weight[1]=1, nearly all traffic should go to h2.
  // A few picks may land on locality 0 when random() returns exactly 0 (0 <= 0 is true).
  const int num_picks = 100;
  auto counts = countPicks(*worker_lb, num_picks);
  EXPECT_GE(counts[h2.get()], num_picks - 2);
}

// Test: Removing all hosts from a locality is handled gracefully.
// The routing snapshot still has weight for the now-empty locality (stale), but
// pickLocalityLb falls back to locality A. No request should return nullptr.
TEST_F(LoadAwareLocalityLbTest, AllHostsRemovedFromLocality) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});

  // Remove h2 from locality B, leaving it empty.
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {h1};
  host_set->healthy_hosts_ = {h1};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{h1}, {}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  priority_set_.runUpdateCallbacks(0, {}, {h2});

  // Every pick must return h1 — pickLocalityLb falls back from the empty locality B to
  // locality A even though the stale routing snapshot still assigns weight to B.
  for (int i = 0; i < 50; ++i) {
    auto result = worker_lb->chooseHost(nullptr);
    ASSERT_NE(nullptr, result.host);
    EXPECT_EQ(result.host, h1);
  }
}

// Test: Stale routing snapshot after host removal — the weighted-random may pick a locality
// whose child LB was torn down, but pickLocalityLb falls back to a locality that still has
// hosts. Verifies no nullptr returns during the window between the host change and the next
// routing weight recomputation.
TEST_F(LoadAwareLocalityLbTest, StaleSnapshotFallbackAfterHostRemoval) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  auto h3 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}, {h3}});

  createLb();

  // Give each locality equal ORCA utilization so they all get equal weight.
  setHostUtilization(*h1, 0.5);
  setHostUtilization(*h2, 0.5);
  setHostUtilization(*h3, 0.5);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Verify all three hosts are reachable before the removal.
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 300));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 300));
  EXPECT_TRUE(hostSeen(*worker_lb, h3, 300));

  // Remove h2 and h3 from their localities, leaving only h1. The routing snapshot still
  // has equal weight for all three localities (stale).
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {h1};
  host_set->healthy_hosts_ = {h1};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{h1}, {}, {}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  priority_set_.runUpdateCallbacks(0, {}, {h2, h3});

  // Despite the stale snapshot, every pick must succeed (fallback to locality A).
  for (int i = 0; i < 200; ++i) {
    auto result = worker_lb->chooseHost(nullptr);
    ASSERT_NE(nullptr, result.host) << "pick " << i << " returned nullptr with stale snapshot";
    EXPECT_EQ(result.host, h1);
  }

  // peekAnotherHost should also never return nullptr.
  for (int i = 0; i < 200; ++i) {
    auto host = worker_lb->peekAnotherHost(nullptr);
    ASSERT_NE(nullptr, host) << "peek " << i << " returned nullptr with stale snapshot";
    EXPECT_EQ(host, h1);
  }
}

// Test: Probe distributes to remotes proportional to host count with 3 localities.
TEST_F(LoadAwareLocalityLbTest, ProbeDistributionThreeLocalities) {
  auto h1 = makeWeightTrackingMockHost();
  // Remote A: 2 hosts
  Upstream::HostVector remote_a_hosts;
  for (int i = 0; i < 2; ++i) {
    remote_a_hosts.push_back(makeWeightTrackingMockHost());
  }
  // Remote B: 4 hosts
  Upstream::HostVector remote_b_hosts;
  for (int i = 0; i < 4; ++i) {
    remote_b_hosts.push_back(makeWeightTrackingMockHost());
  }
  setupLocalities({{h1}, remote_a_hosts, remote_b_hosts}, /*has_local_locality=*/true);

  createLb(/*variance_threshold=*/1.0, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.06);

  setHostUtilization(*h1, 0.5);
  for (auto& host : remote_a_hosts) {
    setHostUtilization(dynamic_cast<Upstream::MockHost&>(*host), 0.5);
  }
  for (auto& host : remote_b_hosts) {
    setHostUtilization(dynamic_cast<Upstream::MockHost&>(*host), 0.5);
  }
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  ASSERT_EQ(3, weights->weights.size());
  EXPECT_TRUE(weights->all_local);

  // 6% probe total, distributed by host count: remote_a=2, remote_b=4 (total remote=6)
  // remote_a gets 2/6 * 0.06 = 0.02, remote_b gets 4/6 * 0.06 = 0.04
  double remote_total = weights->weights[1] + weights->weights[2];
  EXPECT_NEAR(remote_total, 0.06, 0.01);
  // Remote B should get ~2x remote A.
  EXPECT_NEAR(weights->weights[2] / weights->weights[1], 2.0, 0.1);
}

// Test: All localities saturated with unequal host counts — fallback is proportional.
// Locality A has 3 hosts, locality B has 1 host. Traffic should be ~3:1.
TEST_F(LoadAwareLocalityLbTest, AllLocalitiesSaturatedProportionalFallback) {
  Upstream::HostVector locality_a_hosts;
  for (int i = 0; i < 3; ++i) {
    locality_a_hosts.push_back(makeWeightTrackingMockHost());
  }
  auto h_b = makeWeightTrackingMockHost();
  setupLocalities({locality_a_hosts, {h_b}});

  createLb();

  for (auto& host : locality_a_hosts) {
    setHostUtilization(dynamic_cast<Upstream::MockHost&>(*host), 1.0);
  }
  setHostUtilization(*h_b, 1.0);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  // Fallback: weights = [3.0, 1.0], total = 4.0.
  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  EXPECT_NEAR(weights->weights[0], 3.0, 0.01);
  EXPECT_NEAR(weights->weights[1], 1.0, 0.01);
  EXPECT_NEAR(weights->total_weight, 4.0, 0.01);

  auto worker_lb = factory_->create({priority_set_, nullptr});
  auto counts = countPicks(*worker_lb, 400);
  // Locality A (3 hosts) should get ~3x the traffic of locality B.
  int locality_a_count = 0;
  for (const auto& host : locality_a_hosts) {
    locality_a_count += counts[host.get()];
  }
  EXPECT_GT(locality_a_count, counts[h_b.get()] * 2);
}

// Test: peekAnotherHost returns nullptr for an empty LB and delegates for a populated LB.
TEST_F(LoadAwareLocalityLbTest, PeekAnotherHost) {
  createLb();

  // Empty per_locality_ — early return nullptr.
  auto empty_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, empty_lb);
  EXPECT_EQ(nullptr, empty_lb->peekAnotherHost(nullptr));

  // Non-empty per_locality_ — delegates to the child LB and returns selected host.
  auto h1 = makeWeightTrackingMockHost();
  setupLocalities({{h1}});
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());
  auto populated_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, populated_lb);
  EXPECT_EQ(h1, populated_lb->peekAnotherHost(nullptr));
}

// Test: Priority selection fails over beyond priority 0 when higher priorities stay healthy.
TEST_F(LoadAwareLocalityLbTest, HigherPriorityFailoverPreserved) {
  auto h_p0 = makeWeightTrackingMockHost();
  auto h_p1 = makeWeightTrackingMockHost();

  setupPriorityLocalities(0, {{h_p0}}, /*has_local_locality=*/false,
                          std::vector<Upstream::HostVector>{{}});
  setupPriorityLocalities(1, {{h_p1}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  const auto* weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  ASSERT_EQ(2, weights->priority_weights.size());
  EXPECT_EQ(0, weights->priority_loads.healthy_priority_load_.get()[0]);
  EXPECT_EQ(100, weights->priority_loads.healthy_priority_load_.get()[1]);

  for (int i = 0; i < 50; ++i) {
    auto result = worker_lb->chooseHost(nullptr);
    ASSERT_EQ(h_p1.get(), result.host.get());
  }
}

// Test: Priority failover follows health transitions, including fallback to P0 and back to P1.
TEST_F(LoadAwareLocalityLbTest, HigherPriorityFailoverAndFailback) {
  auto h_p0 = makeWeightTrackingMockHost();
  auto h_p1 = makeWeightTrackingMockHost();

  setupPriorityLocalities(0, {{h_p0}}, /*has_local_locality=*/false,
                          std::vector<Upstream::HostVector>{{}});
  setupPriorityLocalities(1, {{h_p1}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  ASSERT_NE(nullptr, typed_factory);

  for (int i = 0; i < 20; ++i) {
    auto result = worker_lb->chooseHost(nullptr);
    ASSERT_EQ(h_p1.get(), result.host.get());
  }

  auto* primary_host_set = priority_set_.getMockHostSet(0);
  primary_host_set->healthy_hosts_ = {h_p0};
  primary_host_set->healthy_hosts_per_locality_ =
      Upstream::makeHostsPerLocality(std::vector<Upstream::HostVector>{{h_p0}},
                                     /*force_no_local_locality=*/true);
  priority_set_.runUpdateCallbacks(0, {}, {});
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  const auto* weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  EXPECT_EQ(100, weights->priority_loads.healthy_priority_load_.get()[0]);
  EXPECT_EQ(0, weights->priority_loads.healthy_priority_load_.get()[1]);

  for (int i = 0; i < 20; ++i) {
    auto result = worker_lb->chooseHost(nullptr);
    ASSERT_EQ(h_p0.get(), result.host.get());
  }

  primary_host_set->healthy_hosts_.clear();
  primary_host_set->healthy_hosts_per_locality_ =
      Upstream::makeHostsPerLocality(std::vector<Upstream::HostVector>{{}},
                                     /*force_no_local_locality=*/true);
  priority_set_.runUpdateCallbacks(0, {}, {});
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  EXPECT_EQ(0, weights->priority_loads.healthy_priority_load_.get()[0]);
  EXPECT_EQ(100, weights->priority_loads.healthy_priority_load_.get()[1]);

  for (int i = 0; i < 20; ++i) {
    auto result = worker_lb->chooseHost(nullptr);
    ASSERT_EQ(h_p1.get(), result.host.get());
  }
}

// Test: Variance threshold NOT triggered when local is overloaded.
// Local util (0.8) >> target_util + threshold → all_local remains false.
TEST_F(LoadAwareLocalityLbTest, VarianceThresholdNotTriggered) {
  // Local: 1 host at 0.8 util. Remote: 10 hosts at 0.2 util.
  // target_util = (0.2*10) / 10 = 0.2  (remote-only average)
  // local_util (0.8) > target_util (0.2) + threshold (0.1) = 0.3 → all_local = false.
  Upstream::HostVector remote_hosts;
  for (int i = 0; i < 10; ++i) {
    remote_hosts.push_back(makeWeightTrackingMockHost());
  }
  auto h_local = makeWeightTrackingMockHost();

  // has_local_locality=true: locality[0] is the local one.
  setupLocalities({{h_local}, remote_hosts}, /*has_local_locality=*/true);

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);

  setHostUtilization(*h_local, 0.8);
  for (auto& host : remote_hosts) {
    setHostUtilization(dynamic_cast<Upstream::MockHost&>(*host), 0.2);
  }
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  // all_local must NOT be triggered: local is overloaded relative to remotes.
  EXPECT_FALSE(weights->all_local);
  // Remote (10 hosts * 0.8 headroom = 8.0) should dominate over local (1 * 0.2 = 0.2).
  EXPECT_GT(weights->weights[1], weights->weights[0]);
}

// Test: selectLocality handles a stale snapshot with more entries than per_locality_.
// This covers the transient window between a topology change and the next weight update.
TEST_F(LoadAwareLocalityLbTest, SnapshotLocalityCountMismatch) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Push a snapshot claiming 3 localities (stale), but per_locality_ only has 2.
  // selectLocality should clamp to min(3, 2) = 2 and not crash.
  auto snapshot = std::make_shared<RoutingWeightsSnapshot>();
  snapshot->weights = {1.0, 1.0, 1.0};
  snapshot->total_weight = 3.0;
  snapshot->priority_weights = {{{1.0, 1.0, 1.0}, 3.0, false, false}};
  snapshot->priority_loads.healthy_priority_load_.get() = {100};
  snapshot->priority_loads.degraded_priority_load_.get() = {0};
  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  typed_factory->updateRoutingWeights(std::move(snapshot));

  // All picks should be valid — no out-of-bounds access, no crash.
  for (int i = 0; i < 100; ++i) {
    auto result = worker_lb->chooseHost(nullptr);
    ASSERT_NE(nullptr, result.host);
    EXPECT_TRUE(result.host == h1 || result.host == h2);
  }
}

// Test: Local locality at 100% utilization — weights[0] == 0 so the variance threshold
// guard (weights[0] > 0.0) prevents setAllLocal(), and traffic routes to remotes.
TEST_F(LoadAwareLocalityLbTest, LocalSaturatedSkipsAllLocal) {
  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);

  createLb(/*variance_threshold=*/1.0, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);

  setHostUtilization(*h_local, 1.0); // fully saturated
  setHostUtilization(*h_remote, 0.3);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);

  // Local headroom = 1 * (1 - 1.0) = 0, so all_local must NOT be triggered.
  EXPECT_FALSE(weights->all_local);
  EXPECT_NEAR(weights->weights[0], 0.0, 1e-9);
  // Remote headroom = 1 * (1 - 0.3) = 0.7.
  EXPECT_NEAR(weights->weights[1], 0.7, 0.01);

  // Nearly all traffic should go to the remote host.
  auto worker_lb = factory_->create({priority_set_, nullptr});
  auto counts = countPicks(*worker_lb, 1000);
  EXPECT_GE(counts[h_remote.get()], 980);
}

// Test: Probe deficit exceeds local weight — the std::max(0, weights[0] - deficit)
// clamp prevents negative weights.
TEST_F(LoadAwareLocalityLbTest, ProbeDeficitClampsPreventsNegativeWeight) {
  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);

  // Local nearly saturated → small weight. High probe_percentage creates a deficit
  // larger than the local weight.
  // local headroom = 1 * (1 - 0.95) = 0.05
  // all_local triggers (0.95 <= 0.5 + 1.0), weights = [1.0, 0.0]
  // probe: remote_target = 1.0 * 0.5 = 0.5, deficit = 0.5
  // Without clamp: weights[0] = 1.0 - 0.5 = 0.5 (no issue here)
  //
  // Use a scenario where headroom-based weight is small:
  // Disable all_local by using has_local_locality=false so weights stay headroom-based.
  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/false);

  // probe_percentage=0.8 → 80% must go to remotes.
  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.8);

  setHostUtilization(*h_local, 0.98); // weight = 1 * 0.02 = 0.02
  setHostUtilization(*h_remote, 0.5); // weight = 1 * 0.5 = 0.5
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);

  // All weights must be non-negative.
  for (double w : weights->weights) {
    EXPECT_GE(w, 0.0);
  }
  EXPECT_GT(weights->total_weight, 0.0);
}

// Test: Healthy hosts diverging from all hosts — capacity estimation uses healthy
// host count, not total. Locality with unhealthy hosts should have reduced weight.
TEST_F(LoadAwareLocalityLbTest, HealthyHostsDivergeFromAll) {
  // Locality A: 4 hosts, only 1 healthy.
  // Locality B: 2 hosts, all healthy.
  Upstream::HostVector locality_a, locality_b;
  for (int i = 0; i < 4; ++i) {
    locality_a.push_back(makeWeightTrackingMockHost());
  }
  for (int i = 0; i < 2; ++i) {
    locality_b.push_back(makeWeightTrackingMockHost());
  }

  // Only the first host in locality A is healthy.
  Upstream::HostVector healthy_a = {locality_a[0]};
  Upstream::HostVector healthy_b = locality_b;

  setupLocalities({locality_a, locality_b}, /*has_local_locality=*/false,
                  std::vector<Upstream::HostVector>{healthy_a, healthy_b});

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);

  // Set utilization on healthy hosts only (unhealthy hosts have no ORCA data).
  setHostUtilization(dynamic_cast<Upstream::MockHost&>(*locality_a[0]), 0.5);
  for (auto& host : locality_b) {
    setHostUtilization(dynamic_cast<Upstream::MockHost&>(*host), 0.5);
  }
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);

  // Locality A: 1 healthy host * (1 - 0.5) = 0.5
  // Locality B: 2 healthy hosts * (1 - 0.5) = 1.0
  // If the code incorrectly used all hosts, A would be 4 * 0.5 = 2.0.
  EXPECT_NEAR(weights->weights[0], 0.5, 0.01);
  EXPECT_NEAR(weights->weights[1], 1.0, 0.01);
}

// --- Tests for empty-delta priority updates ---

// Test: Health-only update (no membership change) re-partitions each locality's child LB.
TEST_F(LoadAwareLocalityLbTest, HealthOnlyUpdateRepartitions) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Both hosts reachable initially.
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 100));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 100));

  // Fire a health-only update (empty hosts_added, empty hosts_removed).
  // This triggers the empty-delta update path, which re-partitions each locality.
  priority_set_.runUpdateCallbacks(0, {}, {});

  // Both hosts should still be reachable after health-only update.
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 100));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 100));
}

// Test: Empty-delta updates still refresh child LB schedulers when host attributes change
// in place (for example host weight).
TEST_F(LoadAwareLocalityLbTest, EmptyDeltaUpdateRefreshesChildWeights) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1, h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  auto initial_counts = countPicks(*worker_lb, 200);
  EXPECT_GT(initial_counts[h1.get()], 50);
  EXPECT_GT(initial_counts[h2.get()], 50);

  h1->weight(100);
  priority_set_.runUpdateCallbacks(0, {}, {});

  auto updated_counts = countPicks(*worker_lb, 400);
  EXPECT_GT(updated_counts[h1.get()], updated_counts[h2.get()] * 10);
  EXPECT_GT(updated_counts[h1.get()], 300);
}

// Test: Health-only updates on higher priorities still refresh the per-priority locality state.
TEST_F(LoadAwareLocalityLbTest, HealthOnlyUpdateHigherPriorityRepartitions) {
  auto h_p0 = makeWeightTrackingMockHost();
  auto h_p1 = makeWeightTrackingMockHost();
  setupPriorityLocalities(0, {{h_p0}}, /*has_local_locality=*/false,
                          std::vector<Upstream::HostVector>{{}});
  setupPriorityLocalities(1, {{h_p1}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  priority_set_.runUpdateCallbacks(1, {}, {});
  EXPECT_TRUE(hostSeen(*worker_lb, h_p1, 50));
}

// Test: Health-only update with topology mismatch is a no-op (waits for onHostChange).
TEST_F(LoadAwareLocalityLbTest, HealthOnlyUpdateTopologyMismatch) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});

  // Mutate the underlying host_set to have 3 localities, simulating a topology change
  // that hasn't yet been processed by onHostChange.
  auto h3 = makeWeightTrackingMockHost();
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {h1, h2, h3};
  host_set->healthy_hosts_ = {h1, h2, h3};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{h1}, {h2}, {h3}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  // Fire health-only update — topology mismatch (3 localities vs 2 in per_locality_)
  // should cause early return, not crash.
  priority_set_.runUpdateCallbacks(0, {}, {});

  // Original hosts still work (per_locality_ unchanged).
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 100));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 100));
}

// --- Tests for edge cases in computeLocalityRoutingWeights ---

// Test: Host set exists but locality_hosts is empty (e.g., all hosts removed).
TEST_F(LoadAwareLocalityLbTest, EmptyLocalityHosts) {
  // Set up an empty locality_hosts vector on host_set 0.
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {};
  host_set->healthy_hosts_ = {};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  createLb();

  // computeLocalityRoutingWeights should return early on empty locality_hosts.
  // No crash, and factory has no routing weights.
  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  ASSERT_NE(nullptr, typed_factory);
  // Weights may be nullptr or empty, but should not crash.
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);
  auto result = worker_lb->chooseHost(nullptr);
  EXPECT_EQ(nullptr, result.host);
}

// Test: All hosts are unhealthy (total_hosts == 0) with local locality → defaults to local.
TEST_F(LoadAwareLocalityLbTest, NoHealthyHostsWithLocalLocality) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();

  // All hosts exist but none are healthy.
  setupLocalities({{h1}, {h2}}, /*has_local_locality=*/true,
                  std::vector<Upstream::HostVector>{{}, {}});

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);

  // total_hosts == 0 with has_local_locality → setAllLocal() is called.
  EXPECT_TRUE(weights->all_local);
  EXPECT_NEAR(weights->weights[0], 1.0, 0.01);
  EXPECT_NEAR(weights->weights[1], 0.0, 0.01);
}

TEST_F(LoadAwareLocalityLbTest, DegradedLocalityWeightsUseDegradedHosts) {
  auto healthy_host = makeWeightTrackingMockHost();
  auto degraded_host = makeWeightTrackingMockHost();

  setupLocalities({{healthy_host}, {degraded_host}},
                  /*has_local_locality=*/false,
                  std::vector<Upstream::HostVector>{{healthy_host}, {}},
                  std::vector<Upstream::HostVector>{{}, {degraded_host}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  ASSERT_NE(nullptr, typed_factory);
  const auto* weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  ASSERT_EQ(2, weights->priority_weights[0].degraded_weights.size());
  EXPECT_NEAR(weights->priority_weights[0].degraded_weights[0], 0.0, 0.01);
  EXPECT_NEAR(weights->priority_weights[0].degraded_weights[1], 1.0, 0.01);

  auto counts = countPicks(*worker_lb, 1000);
  EXPECT_GT(counts[healthy_host.get()], 500);
  EXPECT_GT(counts[degraded_host.get()], 150);
}

TEST_F(LoadAwareLocalityLbTest, PanicRoutingUsesAllHostLocalities) {
  auto healthy_host = makeWeightTrackingMockHost();
  auto unhealthy_host_1 = makeWeightTrackingMockHost();
  auto unhealthy_host_2 = makeWeightTrackingMockHost();

  // 1/3 healthy capacity => 46% effective health after overprovisioning, which is below the
  // default 50% panic threshold. That forces the priority into panic/all-host routing.
  setupLocalities({{healthy_host}, {unhealthy_host_1, unhealthy_host_2}},
                  /*has_local_locality=*/false,
                  std::vector<Upstream::HostVector>{{healthy_host}, {}},
                  std::vector<Upstream::HostVector>{{}, {}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  ASSERT_NE(nullptr, typed_factory);
  const auto* weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  ASSERT_EQ(1, weights->priority_panic.size());
  EXPECT_TRUE(weights->priority_panic[0]);
  ASSERT_EQ(2, weights->priority_weights[0].all_host_weights.size());
  EXPECT_NEAR(weights->priority_weights[0].all_host_weights[0], 1.0, 0.01);
  EXPECT_NEAR(weights->priority_weights[0].all_host_weights[1], 2.0, 0.01);

  auto counts = countPicks(*worker_lb, 400);
  EXPECT_GT(counts[healthy_host.get()], 100);
  EXPECT_GT(counts[unhealthy_host_1.get()] + counts[unhealthy_host_2.get()], 200);
}

// Test: A host transitioning from healthy to degraded updates the per-source child LBs.
// The healthy child LB should lose the host and the degraded child LB should gain it.
TEST_F(LoadAwareLocalityLbTest, HealthTransitionUpdatesPerSourceChildLbs) {
  auto host_a = makeWeightTrackingMockHost();
  auto host_b = makeWeightTrackingMockHost();

  // Both hosts start healthy in separate localities.
  setupLocalities({{host_a}, {host_b}},
                  /*has_local_locality=*/false,
                  std::vector<Upstream::HostVector>{{host_a}, {host_b}},
                  std::vector<Upstream::HostVector>{{}, {}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Both hosts should be reachable (both healthy).
  EXPECT_TRUE(hostSeen(*worker_lb, host_a, 200));
  EXPECT_TRUE(hostSeen(*worker_lb, host_b, 200));

  // Transition host_b from healthy to degraded (health-only update, no membership change).
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->healthy_hosts_ = {host_a};
  host_set->healthy_hosts_per_locality_ =
      Upstream::makeHostsPerLocality(std::vector<Upstream::HostVector>{{host_a}, {}},
                                     /*force_no_local_locality=*/true);
  host_set->degraded_hosts_ = {host_b};
  host_set->degraded_hosts_per_locality_ =
      Upstream::makeHostsPerLocality(std::vector<Upstream::HostVector>{{}, {host_b}},
                                     /*force_no_local_locality=*/true);
  // Health-only update (empty added/removed).
  priority_set_.runUpdateCallbacks(0, {}, {});

  // Recompute routing weights so the priority evaluator sees the new health state.
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  ASSERT_NE(nullptr, typed_factory);
  const auto* weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);

  // Healthy weights: locality 0 has host_a, locality 1 has no healthy hosts.
  ASSERT_EQ(2, weights->priority_weights[0].weights.size());
  EXPECT_GT(weights->priority_weights[0].weights[0], 0.0);
  EXPECT_NEAR(weights->priority_weights[0].weights[1], 0.0, 0.01);

  // Degraded weights: locality 0 has no degraded hosts, locality 1 has host_b.
  ASSERT_EQ(2, weights->priority_weights[0].degraded_weights.size());
  EXPECT_NEAR(weights->priority_weights[0].degraded_weights[0], 0.0, 0.01);
  EXPECT_GT(weights->priority_weights[0].degraded_weights[1], 0.0);

  // Both hosts should still be reachable (host_a via healthy, host_b via degraded routing).
  EXPECT_TRUE(hostSeen(*worker_lb, host_a, 200));
  EXPECT_TRUE(hostSeen(*worker_lb, host_b, 400));
}

// Test: Multi-priority with degraded hosts — priority 0 partially degraded triggers failover
// to priority 1 healthy hosts.
TEST_F(LoadAwareLocalityLbTest, MultiPriorityWithDegradedFailover) {
  auto h_p0_healthy = makeWeightTrackingMockHost();
  auto h_p0_degraded = makeWeightTrackingMockHost();
  auto h_p1_healthy = makeWeightTrackingMockHost();

  // Priority 0: 1 healthy + 1 degraded out of 2 total.
  // Priority 1: 1 healthy out of 1 total.
  setupPriorityLocalities(0, {{h_p0_healthy, h_p0_degraded}},
                          /*has_local_locality=*/false,
                          std::vector<Upstream::HostVector>{{h_p0_healthy}},
                          std::vector<Upstream::HostVector>{{h_p0_degraded}});
  setupPriorityLocalities(1, {{h_p1_healthy}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  ASSERT_NE(nullptr, typed_factory);
  const auto* weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  ASSERT_EQ(2, weights->priority_weights.size());

  // Priority 0 has 50% healthy capacity (1/2 with 1.4x overprovisioning = 70%), so some
  // traffic should fail over to priority 1 via degraded routing.
  // The degraded priority load for P0 should be > 0 or P1 healthy should get some load.
  const uint32_t p0_healthy_load = weights->priority_loads.healthy_priority_load_.get()[0];
  const uint32_t p1_healthy_load = weights->priority_loads.healthy_priority_load_.get()[1];
  const uint32_t p0_degraded_load = weights->priority_loads.degraded_priority_load_.get()[0];

  // P0 shouldn't get 100% healthy load since it's only partially healthy.
  EXPECT_LT(p0_healthy_load, 100u);
  // Either P1 gets healthy failover or P0 gets degraded load (or both).
  EXPECT_GT(p1_healthy_load + p0_degraded_load, 0u);

  // All three hosts should be reachable.
  auto counts = countPicks(*worker_lb, 1000);
  EXPECT_GT(counts[h_p0_healthy.get()], 0);
  EXPECT_GT(counts[h_p0_degraded.get()] + counts[h_p1_healthy.get()], 0);
}

// Test: Lazy child LB creation — degraded child LB is not created when no hosts are degraded,
// but is created when hosts transition to degraded.
TEST_F(LoadAwareLocalityLbTest, LazyChildLbCreationForEmptySources) {
  auto host_a = makeWeightTrackingMockHost();

  // Start with host_a healthy, no degraded hosts.
  setupLocalities({{host_a}},
                  /*has_local_locality=*/false, std::vector<Upstream::HostVector>{{host_a}},
                  std::vector<Upstream::HostVector>{{}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // host_a is reachable via healthy routing.
  EXPECT_TRUE(hostSeen(*worker_lb, host_a, 100));

  // Transition host_a to degraded.
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->healthy_hosts_.clear();
  host_set->healthy_hosts_per_locality_ =
      Upstream::makeHostsPerLocality(std::vector<Upstream::HostVector>{{}},
                                     /*force_no_local_locality=*/true);
  host_set->degraded_hosts_ = {host_a};
  host_set->degraded_hosts_per_locality_ =
      Upstream::makeHostsPerLocality(std::vector<Upstream::HostVector>{{host_a}},
                                     /*force_no_local_locality=*/true);
  priority_set_.runUpdateCallbacks(0, {}, {});
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  // host_a should still be reachable (now via degraded or all-host routing).
  EXPECT_TRUE(hostSeen(*worker_lb, host_a, 400));
}

// Test: Probe redistribution skipped when no remote healthy hosts exist.
TEST_F(LoadAwareLocalityLbTest, ProbeSkippedWhenNoRemoteHealthyHosts) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();

  // Local has healthy hosts, remote has none.
  setupLocalities({{h1}, {h2}}, /*has_local_locality=*/true,
                  std::vector<Upstream::HostVector>{{h1}, {}});

  createLb(/*variance_threshold=*/1.0, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.1);

  setHostUtilization(*h1, 0.5);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);

  // all_local triggers, but probe redistribution is skipped because remote has 0 healthy hosts.
  // All weight stays on local.
  EXPECT_TRUE(weights->all_local);
  EXPECT_NEAR(weights->weights[0], 1.0, 0.01);
  EXPECT_NEAR(weights->weights[1], 0.0, 0.01);
}

// Test: selectLocality with a snapshot that has total_weight == 0 returns 0.
TEST_F(LoadAwareLocalityLbTest, SelectLocalityZeroTotalWeight) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Push a snapshot with zero total weight and empty weights.
  auto snapshot = std::make_shared<RoutingWeightsSnapshot>();
  snapshot->weights = {};
  snapshot->total_weight = 0.0;
  snapshot->priority_weights = {{{}, 0.0, false, false}};
  snapshot->priority_loads.healthy_priority_load_.get() = {100};
  snapshot->priority_loads.degraded_priority_load_.get() = {0};
  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  typed_factory->updateRoutingWeights(std::move(snapshot));

  // selectLocality should return 0 for empty/zero snapshot, selecting locality 0.
  for (int i = 0; i < 20; ++i) {
    auto result = worker_lb->chooseHost(nullptr);
    ASSERT_NE(nullptr, result.host);
    EXPECT_EQ(result.host, h1);
  }
}

// Test: selectLocality with a snapshot smaller than per_locality_ (fewer weights than localities).
TEST_F(LoadAwareLocalityLbTest, SnapshotFewerWeightsThanLocalities) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  auto h3 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}, {h3}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Push a snapshot with only 1 locality weight, but per_locality_ has 3.
  auto snapshot = std::make_shared<RoutingWeightsSnapshot>();
  snapshot->weights = {1.0};
  snapshot->total_weight = 1.0;
  snapshot->priority_weights = {{{1.0}, 1.0, false, false}};
  snapshot->priority_loads.healthy_priority_load_.get() = {100};
  snapshot->priority_loads.degraded_priority_load_.get() = {0};
  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  typed_factory->updateRoutingWeights(std::move(snapshot));

  // selectLocality should clamp to min(1, 3) = 1, all traffic goes to locality 0.
  for (int i = 0; i < 20; ++i) {
    auto result = worker_lb->chooseHost(nullptr);
    ASSERT_NE(nullptr, result.host);
    EXPECT_EQ(result.host, h1);
  }
}

// Test: Local at exact threshold boundary (<=) triggers all_local.
TEST_F(LoadAwareLocalityLbTest, LocalAtExactThreshold) {
  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);

  // threshold = 0.1, local_util = 0.6, remote_util = 0.5
  // local_util (0.6) <= remote_util (0.5) + threshold (0.1) = 0.6 → exact boundary → all_local
  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);

  setHostUtilization(*h_local, 0.6);
  setHostUtilization(*h_remote, 0.5);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);

  // Condition is <= (not <), so exact boundary triggers all_local.
  EXPECT_TRUE(weights->all_local);
}

// Test: EWMA topology change — smoothed state is re-initialized when locality count changes.
TEST_F(LoadAwareLocalityLbTest, EwmaTopologyChangeReinitializes) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/0.3);

  // Initialize smoothed state with 2 localities.
  setHostUtilization(*h1, 0.5);
  setHostUtilization(*h2, 0.5);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  // Now change to 3 localities (topology change).
  auto h3 = makeWeightTrackingMockHost();
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {h1, h2, h3};
  host_set->healthy_hosts_ = {h1, h2, h3};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{h1}, {h2}, {h3}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  setHostUtilization(*h3, 0.8);

  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  ASSERT_NE(nullptr, weights);
  ASSERT_EQ(3, weights->weights.size());
  // Cold start re-init: weights come from raw values, no blending with old smoothed state.
  // h3 at 0.8 util → weight = 1 * 0.2 = 0.2.
  EXPECT_NEAR(weights->weights[2], 0.2, 0.05);
}

// Test: Clamped effective total is zero — all clamped weights are zero.
TEST_F(LoadAwareLocalityLbTest, ClampedEffectiveTotalZero) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Push a snapshot with 3 weights where the first 2 (clamped range) are zero.
  auto snapshot = std::make_shared<RoutingWeightsSnapshot>();
  snapshot->weights = {0.0, 0.0, 5.0};
  snapshot->total_weight = 5.0;
  snapshot->priority_weights = {{{0.0, 0.0, 5.0}, 5.0, false, false}};
  snapshot->priority_loads.healthy_priority_load_.get() = {100};
  snapshot->priority_loads.degraded_priority_load_.get() = {0};
  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  typed_factory->updateRoutingWeights(std::move(snapshot));

  // selectLocality clamps to min(3, 2) = 2, re-sums weights[0..1] = 0.0.
  // effective_total <= 0, so returns locality 0.
  for (int i = 0; i < 20; ++i) {
    auto result = worker_lb->chooseHost(nullptr);
    ASSERT_NE(nullptr, result.host);
    EXPECT_EQ(result.host, h1);
  }
}

// --- Stats tests ---

// Test: lb_recalculate_zone_structures incremented on each timer tick.
TEST_F(LoadAwareLocalityLbTest, StatsRecalculateZoneStructures) {
  auto h1 = makeWeightTrackingMockHost();
  setupLocalities({{h1}});

  createLb();

  // initialize() calls computeLocalityRoutingWeights() once.
  EXPECT_EQ(1, cluster_info_.lbStats().lb_recalculate_zone_structures_.value());

  // Each timer tick increments the counter.
  timer_->invokeCallback();
  EXPECT_EQ(2, cluster_info_.lbStats().lb_recalculate_zone_structures_.value());

  timer_->invokeCallback();
  EXPECT_EQ(3, cluster_info_.lbStats().lb_recalculate_zone_structures_.value());
}

// Test: lb_zone_routing_all_directly incremented when all_local is true and locality 0 chosen.
TEST_F(LoadAwareLocalityLbTest, StatsZoneRoutingAllDirectly) {
  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);

  // Low variance threshold so all_local triggers easily.
  createLb(/*variance_threshold=*/1.0, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);

  setHostUtilization(*h_local, 0.3);
  setHostUtilization(*h_remote, 0.3);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  ASSERT_NE(nullptr, typed_factory->routingWeights());
  EXPECT_TRUE(typed_factory->routingWeights()->all_local);
  EXPECT_TRUE(typed_factory->routingWeights()->has_local_locality);

  auto worker_lb = factory_->create({priority_set_, nullptr});
  const int num_picks = 10;
  for (int i = 0; i < num_picks; ++i) {
    worker_lb->chooseHost(nullptr);
  }

  EXPECT_EQ(num_picks,
            static_cast<int>(cluster_info_.lbStats().lb_zone_routing_all_directly_.value()));
  EXPECT_EQ(0, cluster_info_.lbStats().lb_zone_routing_cross_zone_.value());
  EXPECT_EQ(0, cluster_info_.lbStats().lb_zone_routing_sampled_.value());
}

// Test: lb_zone_routing_cross_zone incremented when a remote locality is chosen.
TEST_F(LoadAwareLocalityLbTest, StatsZoneRoutingCrossZone) {
  auto h_local = makeWeightTrackingMockHost();
  Upstream::HostVector remote_hosts;
  for (int i = 0; i < 10; ++i) {
    remote_hosts.push_back(makeWeightTrackingMockHost());
  }
  setupLocalities({{h_local}, remote_hosts}, /*has_local_locality=*/true);

  // High variance threshold won't trigger, and local is heavily overloaded.
  createLb(/*variance_threshold=*/0.01, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);

  setHostUtilization(*h_local, 0.99);
  for (auto& host : remote_hosts) {
    setHostUtilization(dynamic_cast<Upstream::MockHost&>(*host), 0.1);
  }
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  ASSERT_NE(nullptr, typed_factory->routingWeights());
  EXPECT_FALSE(typed_factory->routingWeights()->all_local);

  auto worker_lb = factory_->create({priority_set_, nullptr});
  const int num_picks = 100;
  for (int i = 0; i < num_picks; ++i) {
    worker_lb->chooseHost(nullptr);
  }

  // Local has weight 1*0.01=0.01, remote has weight 10*0.9=9.0.
  // Nearly all traffic should be cross-zone.
  EXPECT_GT(cluster_info_.lbStats().lb_zone_routing_cross_zone_.value(), 0);
}

// Test: lb_zone_routing_sampled incremented when local locality chosen without all_local.
TEST_F(LoadAwareLocalityLbTest, StatsZoneRoutingSampled) {
  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);

  // Variance threshold is very low so all_local won't trigger, but traffic will still
  // sometimes go to local via weighted random.
  createLb(/*variance_threshold=*/0.0, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);

  // Both at same utilization → all_local won't trigger (0.3 > 0.3 + 0.0 is false, but
  // the condition is <=, so 0.3 <= 0.3 + 0.0 is true). Use local slightly higher.
  setHostUtilization(*h_local, 0.31);
  setHostUtilization(*h_remote, 0.3);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  ASSERT_NE(nullptr, typed_factory->routingWeights());
  EXPECT_FALSE(typed_factory->routingWeights()->all_local);
  EXPECT_TRUE(typed_factory->routingWeights()->has_local_locality);

  auto worker_lb = factory_->create({priority_set_, nullptr});
  const int num_picks = 200;
  for (int i = 0; i < num_picks; ++i) {
    worker_lb->chooseHost(nullptr);
  }

  // With roughly equal weights, some picks go local (sampled) and some cross-zone.
  EXPECT_GT(cluster_info_.lbStats().lb_zone_routing_sampled_.value(), 0);
  EXPECT_GT(cluster_info_.lbStats().lb_zone_routing_cross_zone_.value(), 0);
  EXPECT_EQ(0, cluster_info_.lbStats().lb_zone_routing_all_directly_.value());
}

// Test: No zone routing stats incremented when has_local_locality is false.
TEST_F(LoadAwareLocalityLbTest, StatsNoLocalLocality) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}}, /*has_local_locality=*/false);

  createLb();

  setHostUtilization(*h1, 0.3);
  setHostUtilization(*h2, 0.3);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  ASSERT_NE(nullptr, typed_factory->routingWeights());
  EXPECT_FALSE(typed_factory->routingWeights()->has_local_locality);

  auto worker_lb = factory_->create({priority_set_, nullptr});
  const int num_picks = 100;
  for (int i = 0; i < num_picks; ++i) {
    worker_lb->chooseHost(nullptr);
  }

  // No zone routing stats should be incremented without a local locality.
  EXPECT_EQ(0, cluster_info_.lbStats().lb_zone_routing_all_directly_.value());
  EXPECT_EQ(0, cluster_info_.lbStats().lb_zone_routing_cross_zone_.value());
  EXPECT_EQ(0, cluster_info_.lbStats().lb_zone_routing_sampled_.value());
}

// --- weight_expiration_period tests ---

// Test: Expired ORCA data is ignored, traffic distributes by host count.
TEST_F(LoadAwareLocalityLbTest, WeightExpirationIgnoresStaleData) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  // Timestamp of 1ms (epoch + 1ms) is always older than the 5s expiration window
  // regardless of how long the host has been running.
  const int64_t stale_time_ms = 1;
  setHostUtilizationWithTime(*h1, 0.9, stale_time_ms); // locality 0: high util (stale)
  setHostUtilizationWithTime(*h2, 0.1, stale_time_ms); // locality 1: low util (stale)

  // weight_expiration_period = 5s — both hosts' data is older than 5s.
  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0,
           /*weight_expiration_period=*/std::chrono::milliseconds(5000));

  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // With expired data, both localities should have utilization 0 → equal weight.
  // Traffic should be roughly 50/50.
  const int num_picks = 1000;
  auto counts = countPicks(*worker_lb, num_picks);
  EXPECT_GT(counts[h1.get()], num_picks * 0.3);
  EXPECT_GT(counts[h2.get()], num_picks * 0.3);
}

// Test: Fresh ORCA data within expiration window is used normally.
TEST_F(LoadAwareLocalityLbTest, WeightExpirationFreshDataUsed) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  // Set ORCA data with a recent timestamp.
  const int64_t fresh_time_ms = nowMs();
  setHostUtilizationWithTime(*h1, 0.9, fresh_time_ms); // locality 0: high util
  setHostUtilizationWithTime(*h2, 0.1, fresh_time_ms); // locality 1: low util

  // weight_expiration_period = 5s — data is fresh.
  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0,
           /*weight_expiration_period=*/std::chrono::milliseconds(5000));

  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Fresh data: locality 0 has 10% headroom, locality 1 has 90% headroom.
  // Locality 1 should get the majority of traffic.
  const int num_picks = 1000;
  auto counts = countPicks(*worker_lb, num_picks);
  EXPECT_GT(counts[h2.get()], num_picks * 0.7);
}

// Test: Disabled expiration (0ms) retains data indefinitely.
TEST_F(LoadAwareLocalityLbTest, WeightExpirationDisabled) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  // Timestamp of 1ms — always in the distant past relative to monotonic clock.
  const int64_t stale_time_ms = 1;
  setHostUtilizationWithTime(*h1, 0.9, stale_time_ms);
  setHostUtilizationWithTime(*h2, 0.1, stale_time_ms);

  // weight_expiration_period = 0 (disabled).
  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0,
           /*weight_expiration_period=*/std::chrono::milliseconds(0));

  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Even though data is old, it should still be used because expiration is disabled.
  const int num_picks = 1000;
  auto counts = countPicks(*worker_lb, num_picks);
  EXPECT_GT(counts[h2.get()], num_picks * 0.7);
}

// --- Coverage for defensive guards and error paths ---

// Test: initializeChildLb returns an error when the child factory returns nullptr.
TEST_F(LoadAwareLocalityLbTest, InitializeChildLbNullChild) {
  NiceMock<Upstream::MockTypedLoadBalancerFactory> null_child_factory;
  ON_CALL(null_child_factory, name()).WillByDefault(testing::Return("mock_null_factory"));
  // Return nullptr from create(), simulating an unsupported child policy.
  ON_CALL(null_child_factory,
          create(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .WillByDefault(testing::Return(testing::ByMove(nullptr)));

  auto factory = std::make_shared<WorkerLocalLbFactory>(
      null_child_factory, "mock_null_factory", nullptr, cluster_info_, priority_set_,
      context_.runtime_loader_, random_, context_.time_system_, context_.thread_local_);

  auto status = factory->initializeChildLb();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(), testing::HasSubstr("mock_null_factory"));
}

// Test: onHostChange handles a priority count change (new priority added after construction)
// by rebuilding all per-priority locality state.
TEST_F(LoadAwareLocalityLbTest, OnHostChangePriorityCountMismatch) {
  auto h1 = makeWeightTrackingMockHost();
  setupLocalities({{h1}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 50));

  // Add a new priority level. This makes host_sets.size() (2) != per_priority_locality_.size() (1).
  auto h_p1 = makeWeightTrackingMockHost();
  setupPriorityLocalities(1, {{h_p1}});

  // Fire a host change on the new priority. onHostChange sees the size mismatch and calls
  // buildPerPriorityLocalities() to rebuild all priorities.
  priority_set_.runUpdateCallbacks(1, {h_p1}, {});

  // Push routing weights that send traffic to priority 1.
  auto snapshot = std::make_shared<RoutingWeightsSnapshot>();
  snapshot->priority_weights = {{{1.0}, 1.0, false, false}, {{1.0}, 1.0, false, false}};
  snapshot->priority_loads.healthy_priority_load_.get() = {0, 100};
  snapshot->priority_loads.degraded_priority_load_.get() = {0, 0};
  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  typed_factory->updateRoutingWeights(std::move(snapshot));

  // h_p1 should be reachable via priority 1.
  EXPECT_TRUE(hostSeen(*worker_lb, h_p1, 50));
}

// Test: onInPlaceHostUpdate with a priority count mismatch is a safe no-op.
TEST_F(LoadAwareLocalityLbTest, OnInPlaceHostUpdatePriorityCountMismatch) {
  auto h1 = makeWeightTrackingMockHost();
  setupLocalities({{h1}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Add a new priority level without firing a host change callback.
  auto h_p1 = makeWeightTrackingMockHost();
  setupPriorityLocalities(1, {{h_p1}});

  // Fire an empty-delta update on priority 1. host_sets.size() (2) != per_priority_locality_.size()
  // (1). onInPlaceHostUpdate should early-return without crashing.
  priority_set_.runUpdateCallbacks(1, {}, {});

  // Original host still works.
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 50));
}

// Test: Priority update callbacks with an out-of-range priority are safe no-ops.
// Covers the early-return guard in both onHostChange and onInPlaceHostUpdate.
TEST_F(LoadAwareLocalityLbTest, OutOfRangePriorityCallbacks) {
  auto h1 = makeWeightTrackingMockHost();
  setupLocalities({{h1}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Host-change callback with out-of-range priority (covers onHostChange guard).
  priority_set_.runUpdateCallbacks(5, {makeWeightTrackingMockHost()}, {});

  // Empty-delta callback with out-of-range priority (covers onInPlaceHostUpdate guard).
  priority_set_.runUpdateCallbacks(5, {}, {});

  // Original host still works.
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 50));
}

// Test: All hosts removed from all localities — pickLocalityLb returns nullptr, chooseHost
// returns {nullptr} gracefully (no crash, no UB).
TEST_F(LoadAwareLocalityLbTest, AllLocalitiesEmptiedReturnsNullptr) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();

  setHostUtilization(*h1, 0.5);
  setHostUtilization(*h2, 0.5);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto worker_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  // Verify both hosts work first.
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 200));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 200));

  // Remove all hosts from all localities. The stale snapshot still assigns positive weight
  // to both. pickLocalityLb scans all localities, finds no usable LB, returns nullptr.
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {};
  host_set->healthy_hosts_ = {};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{}, {}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  priority_set_.runUpdateCallbacks(0, {}, {h1, h2});

  // Every pick should return nullptr (no hosts anywhere), not crash.
  for (int i = 0; i < 50; ++i) {
    auto result = worker_lb->chooseHost(nullptr);
    EXPECT_EQ(nullptr, result.host);
  }

  // peekAnotherHost should also return nullptr.
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(nullptr, worker_lb->peekAnotherHost(nullptr));
  }
}

} // namespace
} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
