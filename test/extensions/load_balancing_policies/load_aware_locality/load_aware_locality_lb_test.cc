#include <cstdint>
#include <limits>

#include "source/extensions/load_balancing_policies/load_aware_locality/config.h"
#include "source/extensions/load_balancing_policies/load_aware_locality/load_aware_locality_lb.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/priority_set.h"

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
                double probe_percentage = 0.0) {
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
        rr_factory, std::move(rr_lb_config), weight_update_period, variance_threshold, ewma_alpha,
        probe_percentage, dispatcher_, context_.thread_local_);

    thread_aware_lb_ = std::make_unique<LoadAwareLocalityLoadBalancer>(
        *lb_config, cluster_info_, priority_set_, context_.runtime_loader_, random_,
        context_.time_system_);

    ASSERT_TRUE(thread_aware_lb_->initialize().ok());
    factory_ = thread_aware_lb_->factory();
  }

  // Set up hosts across any number of localities on the mock priority set.
  // When healthy_localities is provided, it overrides the healthy hosts per locality
  // (otherwise healthy == all).
  void setupLocalities(
      std::vector<Upstream::HostVector> localities, bool has_local_locality = false,
      absl::optional<std::vector<Upstream::HostVector>> healthy_localities = absl::nullopt) {
    auto* host_set = priority_set_.getMockHostSet(0);
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
  }

  // Set ORCA utilization on a host.
  void setHostUtilization(Upstream::MockHost& host, double utilization) {
    host.orca_utilization_store_.set(utilization);
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

// Test: EWMA no-data retention — locality with no ORCA data retains previous smoothed value.
TEST_F(LoadAwareLocalityLbTest, EwmaNoDataRetention) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/0.3);

  // First tick: both at 0.8.
  setHostUtilization(*h1, 0.8);
  setHostUtilization(*h2, 0.8);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  // Second tick: remove ORCA data from h2 (simulate stale data by returning 0.0).
  setHostUtilization(*h2, 0.0);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
  auto weights = typed_factory->routingWeights();
  // h2 has no data: smoothed retains 0.8 from first tick.
  // weight[1] = 1 * (1.0 - 0.8) = 0.2 (NOT decaying to 0)
  EXPECT_NEAR(weights->weights[1], 0.2, 0.05);
  EXPECT_GT(weights->weights[1], 0.0);
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

  // h1 should still be reachable — only locality A has hosts.
  for (int i = 0; i < 50; ++i) {
    auto result = worker_lb->chooseHost(nullptr);
    // May return nullptr if child LB for empty locality is chosen, or h1.
    // But locality A should still work.
    if (result.host != nullptr) {
      EXPECT_EQ(result.host, h1);
    }
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

// Test: peekAnotherHost returns nullptr in both empty and non-empty states.
TEST_F(LoadAwareLocalityLbTest, PeekAnotherHost) {
  createLb();

  // Empty per_locality_ — early return nullptr.
  auto empty_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, empty_lb);
  EXPECT_EQ(nullptr, empty_lb->peekAnotherHost(nullptr));

  // Non-empty per_locality_ — delegates to child LB (RoundRobin returns nullptr).
  auto h1 = makeWeightTrackingMockHost();
  setupLocalities({{h1}});
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());
  auto populated_lb = factory_->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, populated_lb);
  EXPECT_EQ(nullptr, populated_lb->peekAnotherHost(nullptr));
}

// Test: Host membership changes at priority > 0 are ignored — only priority 0 is managed.
TEST_F(LoadAwareLocalityLbTest, HigherPriorityHostChangeIgnored) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});

  // Verify initial state: both hosts accessible.
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 100));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 100));

  // Fire a priority=1 host change — should be a no-op for per_locality_ state.
  auto h3 = makeWeightTrackingMockHost();
  priority_set_.runUpdateCallbacks(1, {h3}, {});

  // h1 and h2 still reachable; h3 is NOT (priority 1 is not managed).
  EXPECT_FALSE(hostSeen(*worker_lb, h3));
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

// --- Tests for onHealthChange path ---

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
  // This triggers onHealthChange(0) which re-partitions each locality.
  priority_set_.runUpdateCallbacks(0, {}, {});

  // Both hosts should still be reachable after health-only update.
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 100));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 100));
}

// Test: Health-only update at priority > 0 is ignored.
TEST_F(LoadAwareLocalityLbTest, HealthOnlyUpdateHigherPriorityIgnored) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = factory_->create({priority_set_, nullptr});

  // Fire a health-only update at priority 1 — should be ignored.
  priority_set_.runUpdateCallbacks(1, {}, {});

  // Both hosts still accessible.
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 100));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 100));
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

} // namespace
} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
