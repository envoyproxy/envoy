#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"

#include "source/extensions/load_balancing_policies/load_aware_locality/config.h"
#include "source/extensions/load_balancing_policies/load_aware_locality/load_aware_locality_lb.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/load_balancer.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/mocks/upstream/typed_load_balancer_factory.h"

#include "absl/container/flat_hash_map.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {
namespace {

using testing::NiceMock;
using testing::Return;

std::shared_ptr<NiceMock<Upstream::MockHost>>
makeWeightTrackingMockHost(uint32_t initial_weight = 1) {
  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();
  auto weight = std::make_shared<uint32_t>(initial_weight);
  ON_CALL(*host, weight()).WillByDefault([weight]() -> uint32_t { return *weight; });
  ON_CALL(*host, weight(::testing::_)).WillByDefault([weight](uint32_t new_weight) {
    *weight = new_weight;
  });
  return host;
}

Upstream::HostVector makeHosts(uint32_t count, uint32_t weight = 1) {
  Upstream::HostVector hosts;
  hosts.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    hosts.push_back(makeWeightTrackingMockHost(weight));
  }
  return hosts;
}

class RecordingWorkerChildFactory : public Upstream::LoadBalancerFactory {
public:
  explicit RecordingWorkerChildFactory(bool recreate_on_host_change)
      : recreate_on_host_change_(recreate_on_host_change) {}

  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override {
    ++create_count_;
    return std::make_unique<NiceMock<Upstream::MockLoadBalancer>>();
  }

  bool recreateOnHostChange() const override { return recreate_on_host_change_; }

  uint32_t createCount() const { return create_count_; }

private:
  const bool recreate_on_host_change_;
  uint32_t create_count_{0};
};

class StaticThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
public:
  explicit StaticThreadAwareLoadBalancer(Upstream::LoadBalancerFactorySharedPtr factory)
      : factory_(std::move(factory)) {}

  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override { return absl::OkStatus(); }

private:
  Upstream::LoadBalancerFactorySharedPtr factory_;
};

class LoadAwareLocalityLbTest : public testing::Test {
protected:
  void SetUp() override {
    ON_CALL(context_, mainThreadDispatcher()).WillByDefault(ReturnRef(dispatcher_));
    ON_CALL(random_, random()).WillByDefault([this]() -> uint64_t {
      if (forced_random_index_ < forced_random_values_.size()) {
        return forced_random_values_[forced_random_index_++];
      }
      constexpr uint64_t step = std::numeric_limits<uint64_t>::max() / 100;
      return (random_call_count_++ % 100) * step;
    });
  }

  void forceRandomValues(std::vector<uint64_t> values) {
    forced_random_values_ = std::move(values);
    forced_random_index_ = 0;
  }

  void clearForcedRandomValues() {
    forced_random_values_.clear();
    forced_random_index_ = 0;
  }

  void createLb(double variance_threshold = 0.1, double ewma_alpha = 1.0,
                double probe_percentage = 0.0,
                std::chrono::milliseconds weight_expiration_period = std::chrono::milliseconds(0)) {
    auto weight_update_period = std::chrono::milliseconds(1000);

    envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin round_robin;
    envoy::config::core::v3::TypedExtensionConfig round_robin_config;
    round_robin_config.set_name("envoy.load_balancing_policies.round_robin");
    round_robin_config.mutable_typed_config()->PackFrom(round_robin);

    auto& round_robin_factory =
        Config::Utility::getAndCheckFactory<Upstream::TypedLoadBalancerFactory>(round_robin_config);

    auto round_robin_proto = round_robin_factory.createEmptyConfigProto();
    ASSERT_TRUE(Config::Utility::translateOpaqueConfig(round_robin_config.typed_config(),
                                                       context_.messageValidationVisitor(),
                                                       *round_robin_proto)
                    .ok());
    auto round_robin_lb_config =
        round_robin_factory.loadConfig(context_, *round_robin_proto).value();

    auto lb_config = std::make_unique<LoadAwareLocalityLbConfig>(
        round_robin_factory, round_robin_factory.name(), std::move(round_robin_lb_config),
        weight_update_period, variance_threshold, ewma_alpha, probe_percentage,
        weight_expiration_period, dispatcher_, context_.thread_local_);

    timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);

    thread_aware_lb_ = std::make_unique<LoadAwareLocalityLoadBalancer>(
        *lb_config, cluster_info_, priority_set_, context_.runtime_loader_, random_,
        context_.time_system_);

    ASSERT_TRUE(thread_aware_lb_->initialize().ok());
    factory_ = thread_aware_lb_->factory();
  }

  void setupPriorityLocalities(
      uint32_t priority, std::vector<Upstream::HostVector> localities,
      bool has_local_locality = false,
      absl::optional<std::vector<Upstream::HostVector>> healthy_localities = absl::nullopt,
      absl::optional<std::vector<Upstream::HostVector>> degraded_localities = absl::nullopt) {
    auto* host_set = priority_set_.getMockHostSet(priority);
    Upstream::HostVector all_hosts;
    for (const auto& locality : localities) {
      all_hosts.insert(all_hosts.end(), locality.begin(), locality.end());
    }
    host_set->hosts_ = all_hosts;
    host_set->hosts_per_locality_ =
        Upstream::makeHostsPerLocality(std::move(localities), !has_local_locality);

    if (healthy_localities.has_value()) {
      Upstream::HostVector healthy_hosts;
      for (const auto& locality : *healthy_localities) {
        healthy_hosts.insert(healthy_hosts.end(), locality.begin(), locality.end());
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
      for (const auto& locality : *degraded_localities) {
        degraded_hosts.insert(degraded_hosts.end(), locality.begin(), locality.end());
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

  void setHostUtilization(Upstream::HostSharedPtr host, double utilization) {
    dynamic_cast<Upstream::MockHost&>(*host).orca_utilization_store_.set(utilization, nowMs());
  }

  void setHostUtilizationWithTime(Upstream::HostSharedPtr host, double utilization,
                                  int64_t monotonic_time_ms) {
    dynamic_cast<Upstream::MockHost&>(*host).orca_utilization_store_.set(utilization,
                                                                         monotonic_time_ms);
  }

  void setUtilizationForHosts(const Upstream::HostVector& hosts, double utilization) {
    for (const auto& host : hosts) {
      setHostUtilization(host, utilization);
    }
  }

  void clearHostUtilization(Upstream::HostSharedPtr host) {
    dynamic_cast<Upstream::MockHost&>(*host).orca_utilization_store_.set(0.0, 0);
  }

  int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               context_.time_system_.monotonicTime().time_since_epoch())
        .count();
  }

  Upstream::LoadBalancerPtr createWorkerLb() {
    EXPECT_NE(nullptr, factory_);
    return factory_->create({priority_set_, nullptr});
  }

  const RoutingWeightsSnapshot* routingSnapshot() const {
    auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
    EXPECT_NE(nullptr, typed_factory);
    return typed_factory != nullptr ? typed_factory->routingWeights() : nullptr;
  }

  void publishSnapshot(std::shared_ptr<RoutingWeightsSnapshot> snapshot) {
    auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
    ASSERT_NE(nullptr, typed_factory);
    typed_factory->updateRoutingWeights(std::move(snapshot));
  }

  std::shared_ptr<RoutingWeightsSnapshot>
  makeSnapshot(std::vector<PriorityRoutingWeights> priority_weights,
               std::vector<uint32_t> healthy_priority_load,
               std::vector<uint32_t> degraded_priority_load = {},
               std::vector<bool> priority_panic = {}) {
    auto snapshot = std::make_shared<RoutingWeightsSnapshot>();
    snapshot->priority_weights = std::move(priority_weights);
    if (!snapshot->priority_weights.empty()) {
      const auto& priority_zero = snapshot->priority_weights[0];
      snapshot->weights = priority_zero.weights;
      snapshot->total_weight = priority_zero.total_weight;
      snapshot->all_local = priority_zero.all_local;
      snapshot->has_local_locality = priority_zero.has_local_locality;
    }
    if (degraded_priority_load.empty()) {
      degraded_priority_load.resize(healthy_priority_load.size(), 0);
    }
    snapshot->priority_loads.healthy_priority_load_.get() = std::move(healthy_priority_load);
    snapshot->priority_loads.degraded_priority_load_.get() = std::move(degraded_priority_load);
    snapshot->priority_panic = std::move(priority_panic);
    return snapshot;
  }

  absl::flat_hash_map<const Upstream::Host*, int> countPicks(Upstream::LoadBalancer& lb,
                                                             int num_picks) {
    absl::flat_hash_map<const Upstream::Host*, int> counts;
    for (int i = 0; i < num_picks; ++i) {
      auto result = lb.chooseHost(nullptr);
      EXPECT_NE(nullptr, result.host);
      if (result.host != nullptr) {
        counts[result.host.get()]++;
      }
    }
    return counts;
  }

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

  void expectOnlyHost(Upstream::LoadBalancer& lb, const Upstream::HostConstSharedPtr& host,
                      int picks = 50) {
    for (int i = 0; i < picks; ++i) {
      auto result = lb.chooseHost(nullptr);
      ASSERT_NE(nullptr, result.host);
      EXPECT_EQ(host, result.host);
    }
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterInfo> cluster_info_;
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  NiceMock<Random::MockRandomGenerator> random_;
  uint64_t random_call_count_{0};
  std::vector<uint64_t> forced_random_values_;
  size_t forced_random_index_{0};
  Event::MockTimer* timer_{};

  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
  Upstream::LoadBalancerFactorySharedPtr factory_;
};

TEST_F(LoadAwareLocalityLbTest, EmptyAndSingleLocalitySmoke) {
  createLb();

  auto empty_lb = createWorkerLb();
  ASSERT_NE(nullptr, empty_lb);
  EXPECT_EQ(nullptr, empty_lb->chooseHost(nullptr).host);
  EXPECT_EQ(nullptr, empty_lb->peekAnotherHost(nullptr));

  auto h1 = makeWeightTrackingMockHost();
  setupLocalities({{h1}});
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto populated_lb = createWorkerLb();
  ASSERT_NE(nullptr, populated_lb);
  EXPECT_EQ(h1, populated_lb->chooseHost(nullptr).host);
  EXPECT_EQ(h1, populated_lb->peekAnotherHost(nullptr));
  EXPECT_FALSE(populated_lb->lifetimeCallbacks().has_value());
  std::vector<uint8_t> hash_key;
  EXPECT_FALSE(populated_lb->selectExistingConnection(nullptr, *h1, hash_key).has_value());
}

TEST_F(LoadAwareLocalityLbTest, BasicRoutingWeightsTrackNoDataFreshDataAndZeroUtilization) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_NEAR(snapshot->weights[0], 1.0, 0.01);
  EXPECT_NEAR(snapshot->weights[1], 1.0, 0.01);
  EXPECT_NEAR(snapshot->total_weight, 2.0, 0.01);

  setHostUtilization(h1, 0.9);
  setHostUtilization(h2, 0.1);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_NEAR(snapshot->weights[0], 0.1, 0.01);
  EXPECT_NEAR(snapshot->weights[1], 0.9, 0.01);

  setHostUtilization(h1, 0.0);
  setHostUtilization(h2, 0.9);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_NEAR(snapshot->weights[0], 1.0, 0.01);
  EXPECT_NEAR(snapshot->weights[1], 0.1, 0.01);
}

TEST_F(LoadAwareLocalityLbTest, WeightExpirationIgnoresStaleData) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  setHostUtilizationWithTime(h1, 0.9, 1);
  setHostUtilizationWithTime(h2, 0.1, 1);

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0,
           std::chrono::milliseconds(5000));

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_NEAR(snapshot->weights[0], 1.0, 0.01);
  EXPECT_NEAR(snapshot->weights[1], 1.0, 0.01);

  setHostUtilizationWithTime(h1, 0.9, nowMs());
  setHostUtilizationWithTime(h2, 0.1, nowMs());
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_NEAR(snapshot->weights[0], 0.1, 0.01);
  EXPECT_NEAR(snapshot->weights[1], 0.9, 0.01);
}

TEST_F(LoadAwareLocalityLbTest, WeightExpirationDisabledUsesStaleData) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  setHostUtilizationWithTime(h1, 0.9, 1);
  setHostUtilizationWithTime(h2, 0.1, 1);

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0,
           std::chrono::milliseconds(0));

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_NEAR(snapshot->weights[0], 0.1, 0.01);
  EXPECT_NEAR(snapshot->weights[1], 0.9, 0.01);
}

TEST_F(LoadAwareLocalityLbTest, LocalPreferenceThresholdBoundaries) {
  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);

  setHostUtilization(h_local, 0.3);
  setHostUtilization(h_remote, 0.3);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());
  auto snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_TRUE(snapshot->all_local);
  EXPECT_NEAR(snapshot->weights[0], 1.0, 0.01);
  EXPECT_NEAR(snapshot->weights[1], 0.0, 0.01);

  setHostUtilization(h_local, 0.6);
  setHostUtilization(h_remote, 0.5);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());
  snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_TRUE(snapshot->all_local);

  setHostUtilization(h_local, 0.8);
  setHostUtilization(h_remote, 0.2);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());
  snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_FALSE(snapshot->all_local);
  EXPECT_GT(snapshot->weights[1], snapshot->weights[0]);
}

TEST_F(LoadAwareLocalityLbTest, LocalSaturatedSkipsAllLocal) {
  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);

  createLb(/*variance_threshold=*/1.0, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);

  setHostUtilization(h_local, 1.0);
  setHostUtilization(h_remote, 0.3);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_FALSE(snapshot->all_local);
  EXPECT_NEAR(snapshot->weights[0], 0.0, 1e-9);
  EXPECT_NEAR(snapshot->weights[1], 0.7, 0.01);
}

TEST_F(LoadAwareLocalityLbTest, NoHealthyHostsWithLocalLocalityDefaultsToLocal) {
  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true,
                  std::vector<Upstream::HostVector>{{}, {}});

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_TRUE(snapshot->all_local);
  EXPECT_NEAR(snapshot->weights[0], 1.0, 0.01);
  EXPECT_NEAR(snapshot->weights[1], 0.0, 0.01);
}

TEST_F(LoadAwareLocalityLbTest, ProbeRedistributesToRemoteAndSkipsWhenNoRemoteHealthyHosts) {
  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);

  createLb(/*variance_threshold=*/1.0, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.05);

  setHostUtilization(h_local, 0.3);
  setHostUtilization(h_remote, 1.0);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_TRUE(snapshot->all_local);
  EXPECT_NEAR(snapshot->weights[0], 0.95, 0.01);
  EXPECT_NEAR(snapshot->weights[1], 0.05, 0.01);

  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->healthy_hosts_ = {h_local};
  host_set->healthy_hosts_per_locality_ =
      Upstream::makeHostsPerLocality(std::vector<Upstream::HostVector>{{h_local}, {}},
                                     /*force_no_local_locality=*/false);
  priority_set_.runUpdateCallbacks(0, {}, {});
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_TRUE(snapshot->all_local);
  EXPECT_NEAR(snapshot->weights[0], 1.0, 0.01);
  EXPECT_NEAR(snapshot->weights[1], 0.0, 0.01);
}

TEST_F(LoadAwareLocalityLbTest, ProbeDistributionAcrossMultipleRemoteLocalities) {
  auto local = makeWeightTrackingMockHost();
  auto remote_a = makeHosts(2);
  auto remote_b = makeHosts(4);
  setupLocalities({{local}, remote_a, remote_b}, /*has_local_locality=*/true);

  createLb(/*variance_threshold=*/1.0, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.06);

  setHostUtilization(local, 0.5);
  setUtilizationForHosts(remote_a, 0.5);
  setUtilizationForHosts(remote_b, 0.5);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  ASSERT_EQ(3, snapshot->weights.size());
  EXPECT_TRUE(snapshot->all_local);
  EXPECT_NEAR(snapshot->weights[1], 0.02, 0.01);
  EXPECT_NEAR(snapshot->weights[2], 0.04, 0.01);
}

TEST_F(LoadAwareLocalityLbTest, AllLocalitiesSaturatedFallBacksToHostCount) {
  auto locality_a = makeHosts(3);
  auto locality_b = makeHosts(1);
  setupLocalities({locality_a, locality_b});

  createLb();

  setUtilizationForHosts(locality_a, 1.0);
  setUtilizationForHosts(locality_b, 1.0);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_NEAR(snapshot->weights[0], 3.0, 0.01);
  EXPECT_NEAR(snapshot->weights[1], 1.0, 0.01);

  auto worker_lb = createWorkerLb();
  auto counts = countPicks(*worker_lb, 400);
  int locality_a_count = 0;
  for (const auto& host : locality_a) {
    locality_a_count += counts[host.get()];
  }
  EXPECT_GT(locality_a_count, counts[locality_b[0].get()] * 2);
}

TEST_F(LoadAwareLocalityLbTest, EwmaLifecycleDampensSpikesAndClearsExpiredState) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/0.3, /*probe_percentage=*/0.0,
           std::chrono::milliseconds(5000));

  setHostUtilization(h1, 0.5);
  setHostUtilization(h2, 0.5);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_NEAR(snapshot->weights[0], 0.5, 0.01);
  EXPECT_NEAR(snapshot->weights[1], 0.5, 0.01);

  setHostUtilization(h1, 1.0);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_NEAR(snapshot->weights[0], 0.35, 0.05);
  EXPECT_NEAR(snapshot->weights[1], 0.5, 0.05);

  context_.time_system_.advanceTimeWait(std::chrono::milliseconds(6000));
  clearHostUtilization(h2);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_NEAR(snapshot->weights[1], 1.0, 0.01);
}

TEST_F(LoadAwareLocalityLbTest, EwmaTopologyChangeResetsOnLocalityCountChange) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/0.3);

  setHostUtilization(h1, 0.5);
  setHostUtilization(h2, 0.5);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto h3 = makeWeightTrackingMockHost();
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {h1, h2, h3};
  host_set->healthy_hosts_ = {h1, h2, h3};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{h1}, {h2}, {h3}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;
  setHostUtilization(h3, 0.8);

  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  ASSERT_EQ(3, snapshot->weights.size());
  EXPECT_NEAR(snapshot->weights[2], 0.2, 0.05);
}

TEST_F(LoadAwareLocalityLbTest, HealthyAndDegradedSourcesUseDifferentSnapshots) {
  auto healthy_host = makeWeightTrackingMockHost();
  auto degraded_host = makeWeightTrackingMockHost();

  setupLocalities({{healthy_host}, {degraded_host}},
                  /*has_local_locality=*/false,
                  std::vector<Upstream::HostVector>{{healthy_host}, {}},
                  std::vector<Upstream::HostVector>{{}, {degraded_host}});

  createLb();

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  ASSERT_EQ(2, snapshot->priority_weights[0].degraded_weights.size());
  EXPECT_NEAR(snapshot->priority_weights[0].degraded_weights[0], 0.0, 0.01);
  EXPECT_NEAR(snapshot->priority_weights[0].degraded_weights[1], 1.0, 0.01);

  auto worker_lb = createWorkerLb();
  auto counts = countPicks(*worker_lb, 800);
  EXPECT_GT(counts[healthy_host.get()], 300);
  EXPECT_GT(counts[degraded_host.get()], 100);
}

TEST_F(LoadAwareLocalityLbTest, HealthTransitionRefreshesHealthyAndDegradedChildren) {
  auto healthy_host = makeWeightTrackingMockHost();
  auto remote_host = makeWeightTrackingMockHost();
  setupLocalities({{healthy_host}, {remote_host}},
                  /*has_local_locality=*/false,
                  std::vector<Upstream::HostVector>{{healthy_host}, {remote_host}},
                  std::vector<Upstream::HostVector>{{}, {}});

  createLb();
  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);
  EXPECT_TRUE(hostSeen(*worker_lb, healthy_host, 100));
  EXPECT_TRUE(hostSeen(*worker_lb, remote_host, 100));

  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->healthy_hosts_ = {healthy_host};
  host_set->healthy_hosts_per_locality_ =
      Upstream::makeHostsPerLocality(std::vector<Upstream::HostVector>{{healthy_host}, {}},
                                     /*force_no_local_locality=*/true);
  host_set->degraded_hosts_ = {remote_host};
  host_set->degraded_hosts_per_locality_ =
      Upstream::makeHostsPerLocality(std::vector<Upstream::HostVector>{{}, {remote_host}},
                                     /*force_no_local_locality=*/true);
  priority_set_.runUpdateCallbacks(0, {}, {});
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_GT(snapshot->priority_weights[0].weights[0], 0.0);
  EXPECT_NEAR(snapshot->priority_weights[0].weights[1], 0.0, 0.01);
  EXPECT_NEAR(snapshot->priority_weights[0].degraded_weights[0], 0.0, 0.01);
  EXPECT_GT(snapshot->priority_weights[0].degraded_weights[1], 0.0);

  EXPECT_TRUE(hostSeen(*worker_lb, healthy_host, 200));
  EXPECT_TRUE(hostSeen(*worker_lb, remote_host, 400));
}

TEST_F(LoadAwareLocalityLbTest, PanicUsesAllHosts) {
  auto healthy_host = makeWeightTrackingMockHost();
  auto unhealthy_host_1 = makeWeightTrackingMockHost();
  auto unhealthy_host_2 = makeWeightTrackingMockHost();

  setupLocalities({{healthy_host}, {unhealthy_host_1, unhealthy_host_2}},
                  /*has_local_locality=*/false,
                  std::vector<Upstream::HostVector>{{healthy_host}, {}},
                  std::vector<Upstream::HostVector>{{}, {}});

  createLb();

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  ASSERT_EQ(1, snapshot->priority_panic.size());
  EXPECT_TRUE(snapshot->priority_panic[0]);
  ASSERT_EQ(2, snapshot->priority_weights[0].all_host_weights.size());
  EXPECT_NEAR(snapshot->priority_weights[0].all_host_weights[0], 1.0, 0.01);
  EXPECT_NEAR(snapshot->priority_weights[0].all_host_weights[1], 2.0, 0.01);

  auto worker_lb = createWorkerLb();
  auto counts = countPicks(*worker_lb, 400);
  EXPECT_GT(counts[healthy_host.get()], 100);
  EXPECT_GT(counts[unhealthy_host_1.get()] + counts[unhealthy_host_2.get()], 200);
}

TEST_F(LoadAwareLocalityLbTest, LiveSnapshotRefreshUsesLatestWeights) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 100));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 100));

  PriorityRoutingWeights weights;
  weights.weights = {0.0, 1.0};
  weights.total_weight = 1.0;
  publishSnapshot(makeSnapshot({weights}, {100}));

  auto counts = countPicks(*worker_lb, 100);
  EXPECT_GE(counts[h2.get()], 98);
}

TEST_F(LoadAwareLocalityLbTest, MembershipAndTopologyUpdatesShareTheSameWorkerLb) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 100));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 100));

  auto h3 = makeWeightTrackingMockHost();
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {h1, h2, h3};
  host_set->healthy_hosts_ = {h1, h2, h3};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{h1}, {h2, h3}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;
  priority_set_.runUpdateCallbacks(0, {h3}, {});
  EXPECT_TRUE(hostSeen(*worker_lb, h3, 200));

  host_set->hosts_ = {h1, h3};
  host_set->healthy_hosts_ = {h1, h3};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{h1}, {h3}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;
  priority_set_.runUpdateCallbacks(0, {}, {h2});
  EXPECT_FALSE(hostSeen(*worker_lb, h2, 200));
  EXPECT_TRUE(hostSeen(*worker_lb, h3, 200));

  auto h4 = makeWeightTrackingMockHost();
  host_set->hosts_ = {h1, h3, h4};
  host_set->healthy_hosts_ = {h1, h3, h4};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{h1}, {h3}, {h4}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;
  priority_set_.runUpdateCallbacks(0, {h4}, {});

  PriorityRoutingWeights weights;
  weights.weights = {1.0, 1.0, 1.0};
  weights.total_weight = 3.0;
  publishSnapshot(makeSnapshot({weights}, {100}));
  EXPECT_TRUE(hostSeen(*worker_lb, h4, 300));
}

TEST_F(LoadAwareLocalityLbTest, StaleSnapshotFallbackAndAllLocalitiesRemovedAreGraceful) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  auto h3 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}, {h3}});

  createLb();
  setHostUtilization(h1, 0.5);
  setHostUtilization(h2, 0.5);
  setHostUtilization(h3, 0.5);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 200));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 200));
  EXPECT_TRUE(hostSeen(*worker_lb, h3, 200));

  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {h1};
  host_set->healthy_hosts_ = {h1};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{h1}, {}, {}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;
  priority_set_.runUpdateCallbacks(0, {}, {h2, h3});

  expectOnlyHost(*worker_lb, h1, 100);
  for (int i = 0; i < 50; ++i) {
    auto host = worker_lb->peekAnotherHost(nullptr);
    ASSERT_NE(nullptr, host);
    EXPECT_EQ(h1, host);
    worker_lb->chooseHost(nullptr);
  }

  host_set->hosts_.clear();
  host_set->healthy_hosts_.clear();
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{}, {}, {}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;
  priority_set_.runUpdateCallbacks(0, {}, {h1});

  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(nullptr, worker_lb->chooseHost(nullptr).host);
    EXPECT_EQ(nullptr, worker_lb->peekAnotherHost(nullptr));
  }
}

TEST_F(LoadAwareLocalityLbTest, EmptyDeltaUpdatesRefreshChildWeights) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1, h2}});

  createLb();
  auto worker_lb = createWorkerLb();
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

TEST_F(LoadAwareLocalityLbTest, PriorityFailoverAndFailback) {
  auto h_p0 = makeWeightTrackingMockHost();
  auto h_p1 = makeWeightTrackingMockHost();

  setupPriorityLocalities(0, {{h_p0}}, /*has_local_locality=*/false,
                          std::vector<Upstream::HostVector>{{}});
  setupPriorityLocalities(1, {{h_p1}});

  createLb();
  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);

  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(h_p1, worker_lb->chooseHost(nullptr).host);
  }

  auto* primary_host_set = priority_set_.getMockHostSet(0);
  primary_host_set->healthy_hosts_ = {h_p0};
  primary_host_set->healthy_hosts_per_locality_ =
      Upstream::makeHostsPerLocality(std::vector<Upstream::HostVector>{{h_p0}},
                                     /*force_no_local_locality=*/true);
  priority_set_.runUpdateCallbacks(0, {}, {});
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(h_p0, worker_lb->chooseHost(nullptr).host);
  }

  primary_host_set->healthy_hosts_.clear();
  primary_host_set->healthy_hosts_per_locality_ =
      Upstream::makeHostsPerLocality(std::vector<Upstream::HostVector>{{}},
                                     /*force_no_local_locality=*/true);
  priority_set_.runUpdateCallbacks(0, {}, {});
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(h_p1, worker_lb->chooseHost(nullptr).host);
  }
}

TEST_F(LoadAwareLocalityLbTest, PriorityUpdateGuardPaths) {
  auto h1 = makeWeightTrackingMockHost();
  setupLocalities({{h1}});

  createLb();
  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 50));

  auto h_p1 = makeWeightTrackingMockHost();
  setupPriorityLocalities(1, {{h_p1}});
  priority_set_.runUpdateCallbacks(1, {}, {});
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 50));

  priority_set_.runUpdateCallbacks(1, {h_p1}, {});
  PriorityRoutingWeights p0;
  p0.weights = {1.0};
  p0.total_weight = 1.0;
  PriorityRoutingWeights p1;
  p1.weights = {1.0};
  p1.total_weight = 1.0;
  publishSnapshot(makeSnapshot({p0, p1}, {0, 100}));
  EXPECT_TRUE(hostSeen(*worker_lb, h_p1, 100));

  priority_set_.getMockHostSet(5);
  priority_set_.runUpdateCallbacks(5, {makeWeightTrackingMockHost()}, {});
  priority_set_.runUpdateCallbacks(5, {}, {});
  // Growing the backing priority set without publishing a matching snapshot forces
  // choosePriority() back to firstAvailablePriority(), which is still priority 0.
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 50));
}

TEST_F(LoadAwareLocalityLbTest, ChoosePriorityFallsBackForSnapshotMismatchesAndZeroLoads) {
  auto h1 = makeWeightTrackingMockHost();
  setupLocalities({{h1}});

  createLb();
  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);

  PriorityRoutingWeights weights;
  weights.weights = {1.0};
  weights.total_weight = 1.0;

  publishSnapshot(makeSnapshot({weights, weights}, {50, 50}));
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 20));

  publishSnapshot(makeSnapshot({weights}, {0}));
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 20));

  auto loads_size_mismatch = makeSnapshot({weights}, {50, 50});
  publishSnapshot(loads_size_mismatch);
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 20));
  EXPECT_EQ(h1, worker_lb->peekAnotherHost(nullptr));
}

TEST_F(LoadAwareLocalityLbTest, ChoosePriorityFallsBackWhenSelectedPriorityHasNoLocalities) {
  auto h_p0 = makeWeightTrackingMockHost();
  setupPriorityLocalities(0, {{h_p0}});
  setupPriorityLocalities(1, {});

  createLb();
  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);

  PriorityRoutingWeights p0;
  p0.weights = {1.0};
  p0.total_weight = 1.0;
  PriorityRoutingWeights p1;
  publishSnapshot(makeSnapshot({p0, p1}, {0, 100}));

  expectOnlyHost(*worker_lb, h_p0, 20);
}

TEST_F(LoadAwareLocalityLbTest, SelectLocalityFallsBackForCountMismatchAndZeroTotals) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);

  PriorityRoutingWeights fewer_weights;
  fewer_weights.weights = {1.0};
  fewer_weights.total_weight = 1.0;
  publishSnapshot(makeSnapshot({fewer_weights}, {100}));
  expectOnlyHost(*worker_lb, h1, 20);

  PriorityRoutingWeights zero_total;
  zero_total.total_weight = 0.0;
  publishSnapshot(makeSnapshot({zero_total}, {100}));
  expectOnlyHost(*worker_lb, h1, 20);

  PriorityRoutingWeights clamped_zero;
  clamped_zero.weights = {0.0, 0.0, 5.0};
  clamped_zero.total_weight = 5.0;
  publishSnapshot(makeSnapshot({clamped_zero}, {100}));
  expectOnlyHost(*worker_lb, h1, 20);
}

TEST_F(LoadAwareLocalityLbTest, SelectLocalityRoundingGuardReturnsLastLocality) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  auto h3 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}, {h3}});

  createLb();
  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);

  PriorityRoutingWeights weights;
  weights.weights = {0.2, 0.3, 0.5};
  weights.total_weight = 1.0;
  publishSnapshot(makeSnapshot({weights}, {100}));

  // choosePriority() consumes the first random draw even with a single priority,
  // so the second draw must drive locality selection.
  forceRandomValues({0, std::numeric_limits<uint64_t>::max()});
  EXPECT_EQ(h3, worker_lb->chooseHost(nullptr).host);
}

TEST_F(LoadAwareLocalityLbTest, StatsRecalculateZoneStructuresTracksTimerTicks) {
  auto h1 = makeWeightTrackingMockHost();
  setupLocalities({{h1}});

  createLb();

  EXPECT_EQ(1, cluster_info_.lbStats().lb_recalculate_zone_structures_.value());
  timer_->invokeCallback();
  EXPECT_EQ(2, cluster_info_.lbStats().lb_recalculate_zone_structures_.value());
  timer_->invokeCallback();
  EXPECT_EQ(3, cluster_info_.lbStats().lb_recalculate_zone_structures_.value());
}

TEST_F(LoadAwareLocalityLbTest, ZoneRoutingStatsCoverHealthyRoutingPaths) {
  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);

  createLb(/*variance_threshold=*/1.0, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);
  setHostUtilization(h_local, 0.3);
  setHostUtilization(h_remote, 0.3);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);
  expectOnlyHost(*worker_lb, h_local, 10);
  EXPECT_EQ(10, static_cast<int>(cluster_info_.lbStats().lb_zone_routing_all_directly_.value()));

  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);
  createLb(/*variance_threshold=*/0.0, /*ewma_alpha=*/1.0, /*probe_percentage=*/0.0);
  setHostUtilization(h_local, 0.31);
  setHostUtilization(h_remote, 0.3);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);
  for (int i = 0; i < 200; ++i) {
    worker_lb->chooseHost(nullptr);
  }
  EXPECT_GT(cluster_info_.lbStats().lb_zone_routing_sampled_.value(), 0);
  EXPECT_GT(cluster_info_.lbStats().lb_zone_routing_cross_zone_.value(), 0);
}

TEST_F(LoadAwareLocalityLbTest, ZoneRoutingStatsCoverDegradedAndPanicSources) {
  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}},
                  /*has_local_locality=*/true, std::vector<Upstream::HostVector>{{}, {h_remote}},
                  std::vector<Upstream::HostVector>{{h_local}, {}});

  createLb();
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);
  auto degraded_counts = countPicks(*worker_lb, 400);
  EXPECT_GT(degraded_counts[h_local.get()] + degraded_counts[h_remote.get()], 0);

  auto h_remote_2 = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote, h_remote_2}},
                  /*has_local_locality=*/true, std::vector<Upstream::HostVector>{{h_local}, {}},
                  std::vector<Upstream::HostVector>{{}, {}});

  createLb();
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());
  worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);
  auto panic_counts = countPicks(*worker_lb, 400);
  EXPECT_GT(panic_counts[h_local.get()] + panic_counts[h_remote.get()] +
                panic_counts[h_remote_2.get()],
            0);
  EXPECT_GT(cluster_info_.lbStats().lb_zone_routing_sampled_.value() +
                cluster_info_.lbStats().lb_zone_routing_cross_zone_.value() +
                cluster_info_.lbStats().lb_zone_routing_all_directly_.value(),
            0u);
}

TEST_F(LoadAwareLocalityLbTest, ZoneRoutingStatsStayZeroWithoutLocalLocality) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}}, /*has_local_locality=*/false);

  createLb();
  setHostUtilization(h1, 0.3);
  setHostUtilization(h2, 0.3);
  ASSERT_TRUE(thread_aware_lb_->initialize().ok());

  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);
  for (int i = 0; i < 100; ++i) {
    worker_lb->chooseHost(nullptr);
  }

  EXPECT_EQ(0, cluster_info_.lbStats().lb_zone_routing_all_directly_.value());
  EXPECT_EQ(0, cluster_info_.lbStats().lb_zone_routing_cross_zone_.value());
  EXPECT_EQ(0, cluster_info_.lbStats().lb_zone_routing_sampled_.value());
}

TEST_F(LoadAwareLocalityLbTest, EmptyLocalityHostsDoNotCrash) {
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_.clear();
  host_set->healthy_hosts_.clear();
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  createLb();
  auto worker_lb = createWorkerLb();
  ASSERT_NE(nullptr, worker_lb);
  EXPECT_EQ(nullptr, worker_lb->chooseHost(nullptr).host);
}

TEST_F(LoadAwareLocalityLbTest, InitializeChildLbNullChildReturnsError) {
  NiceMock<Upstream::MockTypedLoadBalancerFactory> null_child_factory;
  ON_CALL(null_child_factory, name()).WillByDefault(Return("mock_null_factory"));
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

TEST_F(LoadAwareLocalityLbTest, ChildRecreatedWhenUnderlyingFactoryRequiresIt) {
  auto local = makeWeightTrackingMockHost();
  auto remote = makeWeightTrackingMockHost();
  setupLocalities({{local}, {remote}});

  auto child_factory = std::make_shared<RecordingWorkerChildFactory>(true);
  NiceMock<Upstream::MockTypedLoadBalancerFactory> typed_child_factory;
  ON_CALL(typed_child_factory, name()).WillByDefault(Return("mock_recreate_child"));
  ON_CALL(typed_child_factory,
          create(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .WillByDefault(testing::Return(
          testing::ByMove(std::make_unique<StaticThreadAwareLoadBalancer>(child_factory))));

  auto factory = std::make_shared<WorkerLocalLbFactory>(
      typed_child_factory, "mock_recreate_child",
      std::make_shared<Upstream::MockTypedLoadBalancerFactory::EmptyLoadBalancerConfig>(),
      cluster_info_, priority_set_, context_.runtime_loader_, random_, context_.time_system_,
      context_.thread_local_);
  ASSERT_TRUE(factory->initializeChildLb().ok());

  auto worker_lb = factory->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);
  const uint32_t initial_create_count = child_factory->createCount();
  EXPECT_GT(initial_create_count, 0);

  auto extra = makeWeightTrackingMockHost();
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {local, remote, extra};
  host_set->healthy_hosts_ = {local, remote, extra};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{local}, {remote, extra}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  priority_set_.runUpdateCallbacks(0, {extra}, {});
  EXPECT_GT(child_factory->createCount(), initial_create_count);
}

} // namespace
} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
