#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"

#include "source/extensions/load_balancing_policies/load_aware_locality/config.h"
#include "source/extensions/load_balancing_policies/load_aware_locality/load_aware_locality_lb.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/load_balancer.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/mocks/upstream/typed_load_balancer_factory.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "xds/data/orca/v3/orca_load_report.pb.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace LoadAwareLocality {
namespace {

using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

// Distinct Locality identity for locality-group index `i`. Region strings sort in lexicographic
// order by index ("r00", "r01", ...) so the default setup order matches a local-first/lexicographic
// ordering.
envoy::config::core::v3::Locality localityForIndex(size_t i) {
  return Upstream::Locality(absl::StrCat("r", absl::Dec(i, absl::kZeroPad2)), "z", "sz");
}

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
  using Inspector = std::function<void(Upstream::LoadBalancerContext&)>;

  explicit RecordingWorkerChildFactory(bool recreate_on_host_change, Inspector inspector = nullptr)
      : recreate_on_host_change_(recreate_on_host_change), inspector_(std::move(inspector)) {}

  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override {
    ++create_count_;
    auto lb = std::make_unique<NiceMock<Upstream::MockLoadBalancer>>();
    Upstream::HostConstSharedPtr host;
    const auto& host_sets = params.priority_set.hostSetsPerPriority();
    if (!host_sets.empty() && !host_sets[0]->hosts().empty()) {
      host = host_sets[0]->hosts()[0];
    }
    ON_CALL(*lb, chooseHost(testing::_))
        .WillByDefault([host, inspector = inspector_](Upstream::LoadBalancerContext* context) {
          if (context != nullptr && inspector) {
            inspector(*context);
          }
          return Upstream::HostSelectionResponse{host};
        });
    ON_CALL(*lb, peekAnotherHost(testing::_))
        .WillByDefault([host, inspector = inspector_](Upstream::LoadBalancerContext* context) {
          if (context != nullptr && inspector) {
            inspector(*context);
          }
          return host;
        });
    return lb;
  }

  bool recreateOnHostChangeDeprecated() const override { return recreate_on_host_change_; }

  uint32_t createCount() const { return create_count_; }

private:
  const bool recreate_on_host_change_;
  const Inspector inspector_;
  uint32_t create_count_{0};
};

// A child LB that always selects asynchronously: chooseHost returns no host plus a cancellation
// handle and (in production) would retain the context to call back later, mimicking dynamic modules
// or dynamic forward proxy. Flips `cancelled` when its handle is cancelled.
class AsyncSelectingChildLb : public Upstream::LoadBalancer {
public:
  explicit AsyncSelectingChildLb(bool& cancelled) : cancelled_(cancelled) {}

  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext*) override {
    return {nullptr, std::make_unique<Handle>(cancelled_)};
  }
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
    return nullptr;
  }
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
    return {};
  }
  std::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext*, const Upstream::Host&,
                           std::vector<uint8_t>&) override {
    return std::nullopt;
  }

private:
  struct Handle : public Upstream::AsyncHostSelectionHandle {
    explicit Handle(bool& cancelled) : cancelled_(cancelled) {}
    void cancel() override { cancelled_ = true; }
    bool& cancelled_;
  };
  bool& cancelled_;
};

class AsyncSelectingChildFactory : public Upstream::LoadBalancerFactory {
public:
  explicit AsyncSelectingChildFactory(bool& cancelled) : cancelled_(cancelled) {}
  Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override {
    return std::make_unique<AsyncSelectingChildLb>(cancelled_);
  }
  bool recreateOnHostChangeDeprecated() const override { return false; }

private:
  bool& cancelled_;
};

uint64_t counterValue(NiceMock<Upstream::MockClusterInfo>& info, const std::string& name) {
  auto c = info.stats_store_.findCounterByString(name);
  return c.has_value() ? c->get().value() : 0;
}

class LoadAwareLocalityLbTest : public Event::TestUsingSimulatedTime, public testing::Test {
protected:
  struct PanicHosts {
    Upstream::HostSharedPtr healthy;
    Upstream::HostSharedPtr unhealthy1;
    Upstream::HostSharedPtr unhealthy2;
  };

  void SetUp() override {
    // Tests run from the simulated-time epoch (monotonic 0): "never reported" is tracked by an
    // out-of-band sentinel, so a report stored at time 0 is a valid sample.
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

  void createLb(double variance_threshold = 0.1, double ewma_alpha = 1.0,
                double remote_probe_fraction = 0.0,
                std::chrono::milliseconds weight_expiration_period = std::chrono::milliseconds(0)) {
    auto weight_update_period = std::chrono::milliseconds(1000);

    envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin round_robin;
    envoy::config::core::v3::TypedExtensionConfig round_robin_config;
    round_robin_config.set_name("envoy.load_balancing_policies.round_robin");
    std::ignore = round_robin_config.mutable_typed_config()->PackFrom(round_robin);

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
        round_robin_factory, std::move(round_robin_lb_config), weight_update_period,
        variance_threshold, ewma_alpha, remote_probe_fraction, weight_expiration_period,
        std::vector<std::string>{}, dispatcher_, context_.thread_local_);

    timer_ = new NiceMock<Event::MockTimer>(&dispatcher_);

    thread_aware_lb_ = std::make_unique<LoadAwareLocalityLoadBalancer>(
        *lb_config, cluster_info_, priority_set_, context_.runtime_loader_, random_,
        context_.time_system_);

    ASSERT_TRUE(thread_aware_lb_->initialize().ok());
    factory_ = thread_aware_lb_->factory();
  }

  std::shared_ptr<WorkerLocalLbFactory>
  makeWorkerFactoryWithChild(Upstream::LoadBalancerFactorySharedPtr child) {
    return std::make_shared<WorkerLocalLbFactory>(
        std::move(child),
        std::make_shared<Upstream::MockTypedLoadBalancerFactory::EmptyLoadBalancerConfig>(),
        cluster_info_, context_.runtime_loader_, random_, context_.thread_local_);
  }

  void setupPriorityLocalities(
      uint32_t priority, std::vector<Upstream::HostVector> localities,
      bool has_local_locality = false,
      std::optional<std::vector<Upstream::HostVector>> healthy_localities = std::nullopt,
      std::optional<std::vector<Upstream::HostVector>> degraded_localities = std::nullopt) {
    auto* host_set = priority_set_.getMockHostSet(priority);
    // Stamp each host with its locality-group identity so the policy can key weights by Locality.
    for (size_t i = 0; i < localities.size(); ++i) {
      for (const auto& host : localities[i]) {
        setHostLocality(host, i);
      }
    }
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
      std::optional<std::vector<Upstream::HostVector>> healthy_localities = std::nullopt,
      std::optional<std::vector<Upstream::HostVector>> degraded_localities = std::nullopt) {
    setupPriorityLocalities(0, std::move(localities), has_local_locality,
                            std::move(healthy_localities), std::move(degraded_localities));
  }

  std::pair<Upstream::HostSharedPtr, Upstream::HostSharedPtr>
  setupTwoLocalityLb(std::chrono::milliseconds weight_expiration_period, bool has_local = false) {
    auto h1 = makeWeightTrackingMockHost();
    auto h2 = makeWeightTrackingMockHost();
    setupLocalities({{h1}, {h2}}, has_local);
    createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*remote_probe_fraction=*/0.0,
             weight_expiration_period);
    return {h1, h2};
  }

  PanicHosts setupPanicHosts(bool has_local_locality = false) {
    PanicHosts hosts{makeWeightTrackingMockHost(), makeWeightTrackingMockHost(),
                     makeWeightTrackingMockHost()};
    setupLocalities({{hosts.healthy}, {hosts.unhealthy1, hosts.unhealthy2}}, has_local_locality,
                    std::vector<Upstream::HostVector>{{hosts.healthy}, {}},
                    std::vector<Upstream::HostVector>{{}, {}});
    return hosts;
  }

  void setHostLocality(const Upstream::HostSharedPtr& host, size_t i) {
    setHostLocalityExplicit(host, localityForIndex(i));
  }

  void setHostLocalityExplicit(const Upstream::HostSharedPtr& host,
                               const envoy::config::core::v3::Locality& locality) {
    auto* mock = dynamic_cast<Upstream::MockHost*>(host.get());
    if (mock == nullptr) {
      return;
    }
    localities_.push_back(locality);
    ON_CALL(*mock, locality()).WillByDefault(testing::ReturnRef(localities_.back()));
  }

  static LocalityRoutingWeightsMap makeWeightsMap(const std::vector<double>& weights) {
    LocalityRoutingWeightsMap map;
    for (size_t i = 0; i < weights.size(); ++i) {
      map[localityForIndex(i)] = weights[i];
    }
    return map;
  }

  void recomputeWeights() { ASSERT_TRUE(thread_aware_lb_->initialize().ok()); }

  // Reshapes a priority's host set, then fires the membership update callback. Two modes:
  //  - full membership reshape: delegates to setupPriorityLocalities (healthy mirrors all hosts).
  //  - health-only flip (when `healthy_override` is set): leaves hosts_/hosts_per_locality_ intact
  //    and only sets healthy_hosts_/healthy_hosts_per_locality_ from `healthy_override`.
  void reshapeLocalities(
      uint32_t priority, std::vector<Upstream::HostVector> localities,
      Upstream::HostVector added = {}, Upstream::HostVector removed = {}, bool has_local = false,
      std::optional<std::vector<Upstream::HostVector>> healthy_override = std::nullopt) {
    if (healthy_override.has_value()) {
      auto* host_set = priority_set_.getMockHostSet(priority);
      Upstream::HostVector healthy_hosts;
      for (const auto& locality : *healthy_override) {
        healthy_hosts.insert(healthy_hosts.end(), locality.begin(), locality.end());
      }
      host_set->healthy_hosts_ = healthy_hosts;
      host_set->healthy_hosts_per_locality_ =
          Upstream::makeHostsPerLocality(std::move(*healthy_override), !has_local);
    } else {
      setupPriorityLocalities(priority, std::move(localities), has_local);
    }
    priority_set_.runUpdateCallbacks(priority, added, removed);
  }

  static xds::data::orca::v3::OrcaLoadReport makeOrcaReport(double app_utilization) {
    xds::data::orca::v3::OrcaLoadReport report;
    report.set_application_utilization(app_utilization);
    return report;
  }

  void setHostUtilization(Upstream::HostSharedPtr host, double utilization) {
    auto host_data = host->typedLbPolicyData<LocalityLbHostData>();
    ASSERT(host_data.has_value());
    EXPECT_TRUE(host_data->onOrcaLoadReport(makeOrcaReport(utilization), stream_info_).ok());
  }

  void setUtilizationForHosts(const Upstream::HostVector& hosts, double utilization) {
    for (const auto& host : hosts) {
      setHostUtilization(host, utilization);
    }
  }

  Upstream::LoadBalancerPtr createWorkerLb() {
    EXPECT_NE(nullptr, factory_);
    auto worker_lb = factory_->create({priority_set_, nullptr});
    EXPECT_NE(nullptr, worker_lb);
    return worker_lb;
  }

  const RoutingWeightsSnapshot* routingSnapshot() const {
    auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
    EXPECT_NE(nullptr, typed_factory);
    const auto* shim = typed_factory != nullptr ? typed_factory->tlsShim() : nullptr;
    return shim != nullptr ? shim->routing_weights.get() : nullptr;
  }

  void publishSnapshot(std::shared_ptr<RoutingWeightsSnapshot> snapshot) {
    auto* typed_factory = dynamic_cast<WorkerLocalLbFactory*>(factory_.get());
    ASSERT_NE(nullptr, typed_factory);
    typed_factory->updateRoutingWeights(std::move(snapshot));
  }

  // Builds an advisory locality-weights snapshot. Priority/health/panic selection is now live
  // worker state (LoadBalancerBase), so the snapshot only carries per-priority locality weights.
  std::shared_ptr<RoutingWeightsSnapshot>
  makeSnapshot(std::vector<PriorityRoutingWeights> priority_weights) {
    auto snapshot = std::make_shared<RoutingWeightsSnapshot>();
    snapshot->priority_weights = std::move(priority_weights);
    return snapshot;
  }

  // Publishes a single-priority snapshot with Healthy weights keyed by per-index Locality identity.
  void publishHealthyWeights(const std::vector<double>& weights) {
    PriorityRoutingWeights pw;
    auto& healthy =
        pw.by_source[static_cast<size_t>(PriorityRoutingWeights::SelectionSource::Healthy)];
    healthy.weights = makeWeightsMap(weights);
    publishSnapshot(makeSnapshot({pw}));
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

  // Asserts the live routing snapshot's `source` weights for `priority` equal `expected`, matching
  // each entry by Locality identity (expected[i] ↔ localityForIndex(i)). A missing identity reads
  // as 0.0 (a locality not in the snapshot map carries no weight). Asserts the snapshot is
  // non-null.
  void expectWeights(PriorityRoutingWeights::SelectionSource source, std::vector<double> expected,
                     double tol = 0.01, uint32_t priority = 0) {
    const auto* snapshot = routingSnapshot();
    ASSERT_NE(nullptr, snapshot);
    const auto& weights = snapshot->priority_weights[priority].weightsFor(source);
    for (size_t i = 0; i < expected.size(); ++i) {
      SCOPED_TRACE(absl::StrCat("locality ", i));
      auto it = weights.find(localityForIndex(i));
      const double actual = it != weights.end() ? it->second : 0.0;
      EXPECT_NEAR(actual, expected[i], tol);
    }
  }

  // Reads the snapshot weight for locality-group index `i` (by identity); 0.0 if absent.
  double weightForIndex(PriorityRoutingWeights::SelectionSource source, size_t i,
                        uint32_t priority = 0) {
    const auto* snapshot = routingSnapshot();
    EXPECT_NE(nullptr, snapshot);
    if (snapshot == nullptr) {
      return 0.0;
    }
    const auto& weights = snapshot->priority_weights[priority].weightsFor(source);
    auto it = weights.find(localityForIndex(i));
    return it != weights.end() ? it->second : 0.0;
  }

  void expectAllLocal(bool expected,
                      PriorityRoutingWeights::SelectionSource source =
                          PriorityRoutingWeights::SelectionSource::Healthy,
                      uint32_t priority = 0) {
    const auto* snapshot = routingSnapshot();
    ASSERT_NE(nullptr, snapshot);
    EXPECT_EQ(expected, snapshot->priority_weights[priority].allLocalFor(source));
  }

  void expectCounterIncrementsPerTick(const std::string& name, uint64_t delta) {
    const uint64_t before = counterValue(cluster_info_, name);
    timer_->invokeCallback();
    EXPECT_EQ(before + delta, counterValue(cluster_info_, name));
  }

  uint64_t zoneRoutingStatTotal() const {
    return cluster_info_.lbStats().lb_zone_routing_all_directly_.value() +
           cluster_info_.lbStats().lb_zone_routing_sampled_.value() +
           cluster_info_.lbStats().lb_zone_routing_cross_zone_.value();
  }

  void expectTwoHostSnapCounter(const std::string& counter, double variance_threshold,
                                double remote_probe_fraction,
                                std::optional<std::pair<double, double>> quiet_utilizations,
                                std::pair<double, double> active_utilizations,
                                uint64_t startup_count = 1) {
    auto h_local = makeWeightTrackingMockHost();
    auto h_remote = makeWeightTrackingMockHost();
    setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);

    createLb(variance_threshold, /*ewma_alpha=*/1.0, remote_probe_fraction);
    const uint64_t after_create = counterValue(cluster_info_, counter);
    EXPECT_EQ(startup_count, after_create);

    if (quiet_utilizations.has_value()) {
      setHostUtilization(h_local, quiet_utilizations->first);
      setHostUtilization(h_remote, quiet_utilizations->second);
      recomputeWeights();
      EXPECT_EQ(after_create, counterValue(cluster_info_, counter));
    }

    const uint64_t before_active = counterValue(cluster_info_, counter);
    setHostUtilization(h_local, active_utilizations.first);
    setHostUtilization(h_remote, active_utilizations.second);
    recomputeWeights();
    EXPECT_EQ(before_active + 1, counterValue(cluster_info_, counter));
    expectCounterIncrementsPerTick(counter, 1);
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> context_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterInfo> cluster_info_;
  NiceMock<Upstream::MockPrioritySet> priority_set_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  uint64_t random_call_count_{0};
  std::vector<uint64_t> forced_random_values_;
  size_t forced_random_index_{0};
  Event::MockTimer* timer_{};
  // Stable storage for Locality references returned by mock hosts.
  std::deque<envoy::config::core::v3::Locality> localities_;

  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
  Upstream::LoadBalancerFactorySharedPtr factory_;
};

// Single-tick weight-computation cases; multi-tick and routing-sampling scenarios stay dedicated.
struct WeightComputationCase {
  std::string test_name;
  std::vector<int> locality_sizes; // host count per locality group
  bool has_local_locality;
  double variance_threshold;
  double remote_probe_fraction;
  std::vector<double> locality_utils; // utilization applied to every host in the locality
  bool expect_all_local;
  std::vector<double> expected_healthy_weights;
  double tol;
};

class WeightComputationTest : public LoadAwareLocalityLbTest,
                              public testing::WithParamInterface<WeightComputationCase> {};

TEST_P(WeightComputationTest, ComputesHealthyWeights) {
  const WeightComputationCase& c = GetParam();
  std::vector<Upstream::HostVector> localities;
  for (int size : c.locality_sizes) {
    localities.push_back(makeHosts(size));
  }
  setupLocalities(localities, c.has_local_locality);
  createLb(c.variance_threshold, /*ewma_alpha=*/1.0, c.remote_probe_fraction);

  for (size_t i = 0; i < localities.size(); ++i) {
    setUtilizationForHosts(localities[i], c.locality_utils[i]);
  }
  recomputeWeights();

  expectAllLocal(c.expect_all_local);
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, c.expected_healthy_weights,
                c.tol);
}

INSTANTIATE_TEST_SUITE_P(WeightComputation, WeightComputationTest,
                         testing::ValuesIn<WeightComputationCase>({
                             {"ScaleWithEligibleHostCounts",
                              {10, 2},
                              false,
                              0.1,
                              0.0,
                              {0.5, 0.5},
                              false,
                              {5.0, 1.0},
                              0.01},
                             {"AllLocalitiesSaturatedFallBacksToHostCount",
                              {3, 1},
                              false,
                              0.1,
                              0.0,
                              {1.0, 1.0},
                              false,
                              {3.0, 1.0},
                              0.01},
                             // All-overloaded fallback is checked before the local-preference
                             // snap: a saturated local locality spreads by host count, not
                             // all-local.
                             {"AllSaturatedWithLocalSpreadsByHostCountNotSnap",
                              {3, 1},
                              true,
                              0.1,
                              0.0,
                              {1.0, 1.0},
                              false,
                              {3.0, 1.0},
                              0.01},
                             {"LocalSaturatedSpillsBeyondThreshold",
                              {1, 1},
                              true,
                              0.1,
                              0.0,
                              {1.0, 0.3},
                              false,
                              {0.0, 0.7},
                              0.01},
                             {"SaturatedLocalWithinThresholdSnapsLocal",
                              {1, 1},
                              true,
                              0.1,
                              0.0,
                              {1.0, 0.95},
                              true,
                              {1.0, 0.0},
                              0.01},
                             {"ProbeDistributionAcrossRemotes",
                              {1, 2, 4},
                              true,
                              1.0,
                              0.06,
                              {0.5, 0.5, 0.5},
                              false,
                              {0.94, 0.02, 0.04},
                              0.01},
                             {"ProbeHostCountProportionalUnequalUtils",
                              {1, 2, 4},
                              true,
                              1.0,
                              0.06,
                              {0.5, 0.8, 0.2},
                              false,
                              {0.94, 0.02, 0.04},
                              0.01},
                             {"ProbeRedistributionClampsLocalAtZero",
                              {1, 1},
                              true,
                              0.0,
                              2.0,
                              {0.3, 0.0},
                              false,
                              {0.0, 1.7},
                              0.01},
                             {"LocalPreferenceRemoteHostWeightedTarget",
                              {1, 9, 1},
                              true,
                              0.01,
                              0.0,
                              {0.5, 0.6, 0.0},
                              true,
                              {1.0, 0.0, 0.0},
                              0.01},
                         }),
                         [](const testing::TestParamInfo<WeightComputationCase>& info) {
                           return info.param.test_name;
                         });

TEST_F(LoadAwareLocalityLbTest, EmptyAndSingleLocalitySmoke) {
  createLb();

  auto empty_lb = createWorkerLb();
  ASSERT_NE(nullptr, empty_lb);
  EXPECT_EQ(nullptr, empty_lb->chooseHost(nullptr).host);

  auto h1 = makeWeightTrackingMockHost();
  setupLocalities({{h1}});
  recomputeWeights();

  auto populated_lb = createWorkerLb();
  ASSERT_NE(nullptr, populated_lb);
  EXPECT_EQ(h1, populated_lb->chooseHost(nullptr).host);
  EXPECT_FALSE(populated_lb->lifetimeCallbacks().has_value());
  std::vector<uint8_t> hash_key;
  EXPECT_FALSE(populated_lb->selectExistingConnection(nullptr, *h1, hash_key).has_value());
}

TEST_F(LoadAwareLocalityLbTest, BasicRoutingWeightsTrackNoDataFreshDataAndZeroUtilization) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();

  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {1.0, 1.0});

  setHostUtilization(h1, 0.9);
  setHostUtilization(h2, 0.1);
  recomputeWeights();
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {0.1, 0.9});

  // Zero-utilization reports carry no positive signal and do not refresh host data.
  setHostUtilization(h1, 0.0);
  setHostUtilization(h2, 0.9);
  recomputeWeights();
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {0.1, 0.1});
}

TEST_F(LoadAwareLocalityLbTest, WeightExpirationIgnoresStaleData) {
  auto [h1, h2] = setupTwoLocalityLb(std::chrono::milliseconds(5000));

  recomputeWeights();

  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {1.0, 1.0});

  setHostUtilization(h1, 0.9);
  setHostUtilization(h2, 0.1);
  recomputeWeights();
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {0.1, 0.9});

  // Advance time past the 5s expiration window. The stale data is ignored, and weights
  // fall back to host counts (1.0, 1.0).
  context_.time_system_.advanceTimeWait(std::chrono::milliseconds(6000));
  recomputeWeights();
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {1.0, 1.0});
}

TEST_F(LoadAwareLocalityLbTest, WeightExpirationDisabledUsesStaleData) {
  // Expiration disabled (0ms) — data is always used regardless of age.
  auto [h1, h2] = setupTwoLocalityLb(std::chrono::milliseconds(0));

  setHostUtilization(h1, 0.9);
  setHostUtilization(h2, 0.1);
  recomputeWeights();
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {0.1, 0.9});

  context_.time_system_.advanceTimeWait(std::chrono::milliseconds(6000));
  recomputeWeights();
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {0.1, 0.9});
}

// Regression: a report stored at monotonic time 0 (the simulated-time epoch) is a valid sample —
// "never reported" is tracked by an out-of-band sentinel, not timestamp 0.
TEST_F(LoadAwareLocalityLbTest, ReportAtMonotonicTimeZeroIsValid) {
  auto [h1, h2] = setupTwoLocalityLb(std::chrono::milliseconds(5000));

  ASSERT_EQ(0, std::chrono::duration_cast<std::chrono::milliseconds>(
                   simTime().monotonicTime().time_since_epoch())
                   .count());
  setHostUtilization(h1, 0.9);
  setHostUtilization(h2, 0.1);
  recomputeWeights();
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {0.1, 0.9});
}

TEST_F(LoadAwareLocalityLbTest, StaleRemoteCarriesPriorNotZero) {
  auto [h_local, h_remote] =
      setupTwoLocalityLb(std::chrono::milliseconds(5000), /*has_local=*/true);

  setHostUtilization(h_local, 0.5);
  setHostUtilization(h_remote, 0.6);
  recomputeWeights();

  expectAllLocal(true);

  // The stale remote's last-known load must be carried, not reset to idle.
  context_.time_system_.advanceTimeWait(std::chrono::milliseconds(6000));
  setHostUtilization(h_local, 0.5);
  recomputeWeights();

  expectAllLocal(true);
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {1.0, 0.0});
}

TEST_F(LoadAwareLocalityLbTest, StaleLocalityHostCountWeighted) {
  auto locality_a = makeHosts(3);
  auto locality_b = makeHosts(2);
  setupLocalities({locality_a, locality_b});

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*remote_probe_fraction=*/0.0,
           std::chrono::milliseconds(5000));

  setUtilizationForHosts(locality_a, 0.8);
  setUtilizationForHosts(locality_b, 0.5);
  recomputeWeights();

  // Stale locality_b falls back to its host-count baseline.
  context_.time_system_.advanceTimeWait(std::chrono::milliseconds(6000));
  setUtilizationForHosts(locality_a, 0.8);
  recomputeWeights();
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {0.6, 2.0});
}

TEST_F(LoadAwareLocalityLbTest, HealthyWeightsCanDivergeFromAllHostWeights) {
  auto all_a = makeHosts(4);
  auto all_b = makeHosts(2);
  Upstream::HostVector healthy_a = {all_a[0]};
  Upstream::HostVector healthy_b = {all_b[0], all_b[1]};
  setupLocalities({all_a, all_b}, /*has_local_locality=*/false,
                  std::vector<Upstream::HostVector>{healthy_a, healthy_b});

  createLb();

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  ASSERT_EQ(2, snapshot->priority_weights[0].by_source[0].weights.size());
  ASSERT_EQ(2,
            snapshot->priority_weights[0]
                .by_source[static_cast<size_t>(PriorityRoutingWeights::SelectionSource::AllHosts)]
                .weights.size());
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {1.0, 2.0});
  expectWeights(PriorityRoutingWeights::SelectionSource::AllHosts, {4.0, 2.0});
}

TEST_F(LoadAwareLocalityLbTest, WeightComputationSkipsEmptyLocalitiesAndHostsWithoutPolicyData) {
  auto h1 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {}});
  createLb();

  setHostUtilization(h1, 1.0);
  recomputeWeights();
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {1.0, 0.0});

  auto h_without_data = makeWeightTrackingMockHost();
  setHostLocality(h_without_data, 2);
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {h1, h_without_data};
  host_set->healthy_hosts_ = {h1, h_without_data};
  host_set->hosts_per_locality_ = Upstream::makeHostsPerLocality({{h1}, {}, {h_without_data}},
                                                                 /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  timer_->invokeCallback();
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {0.0, 0.0, 1.0});
}

TEST_F(LoadAwareLocalityLbTest, EwmaLifecycleDampensSpikesAndClearsExpiredState) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/0.3, /*remote_probe_fraction=*/0.0,
           std::chrono::milliseconds(5000));

  setHostUtilization(h1, 0.5);
  setHostUtilization(h2, 0.5);
  recomputeWeights();
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {0.5, 0.5});

  setHostUtilization(h1, 1.0);
  recomputeWeights();
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {0.35, 0.5}, 0.05);

  context_.time_system_.advanceTimeWait(std::chrono::milliseconds(6000));
  recomputeWeights();

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_NEAR(weightForIndex(PriorityRoutingWeights::SelectionSource::Healthy, 1), 1.0, 0.01);
}

TEST_F(LoadAwareLocalityLbTest, EwmaStateFollowsLocalityIdentityOnSameCountSwap) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/0.3);

  setHostUtilization(h1, 0.5);
  setHostUtilization(h2, 0.9);
  recomputeWeights();
  expectWeights(PriorityRoutingWeights::SelectionSource::Healthy, {0.5, 0.1});

  // Replace locality index 1 (h2's zone) with a brand-new locality (h3) — locality COUNT is
  // unchanged, so index-keyed state would silently attribute h2's 0.9 EWMA to h3.
  auto h3 = makeWeightTrackingMockHost();
  setHostLocality(h3, 2);
  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {h1, h3};
  host_set->healthy_hosts_ = {h1, h3};
  host_set->hosts_per_locality_ =
      Upstream::makeHostsPerLocality({{h1}, {h3}}, /*force_no_local_locality=*/true);
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  // Re-initialize to attach LocalityLbHostData to the new host, then report. The first sample
  // must be applied raw (0.9 weight), not EWMA-blended with the departed locality's 0.9 util
  // (which would give 0.3*0.1 + 0.7*0.9 = 0.66 → weight 0.34).
  recomputeWeights();
  setHostUtilization(h3, 0.1);
  recomputeWeights();
  EXPECT_NEAR(weightForIndex(PriorityRoutingWeights::SelectionSource::Healthy, 0), 0.5, 0.05);
  EXPECT_NEAR(weightForIndex(PriorityRoutingWeights::SelectionSource::Healthy, 2), 0.9, 0.05);
  // The departed locality's identity carries no snapshot entry (its EWMA state was dropped).
  EXPECT_EQ(0.0, weightForIndex(PriorityRoutingWeights::SelectionSource::Healthy, 1));
}

TEST_F(LoadAwareLocalityLbTest, HealthyAndDegradedSourcesUseDifferentSnapshots) {
  auto healthy_host = makeWeightTrackingMockHost();
  auto degraded_host = makeWeightTrackingMockHost();

  setupLocalities({{healthy_host}, {degraded_host}},
                  /*has_local_locality=*/false,
                  std::vector<Upstream::HostVector>{{healthy_host}, {}},
                  std::vector<Upstream::HostVector>{{}, {degraded_host}});

  createLb();

  expectWeights(PriorityRoutingWeights::SelectionSource::Degraded, {0.0, 1.0});

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
  recomputeWeights();

  const auto* snapshot = routingSnapshot();
  ASSERT_NE(nullptr, snapshot);
  EXPECT_GT(weightForIndex(PriorityRoutingWeights::SelectionSource::Healthy, 0), 0.0);
  EXPECT_NEAR(weightForIndex(PriorityRoutingWeights::SelectionSource::Healthy, 1), 0.0, 0.01);
  EXPECT_NEAR(weightForIndex(PriorityRoutingWeights::SelectionSource::Degraded, 0), 0.0, 0.01);
  EXPECT_GT(weightForIndex(PriorityRoutingWeights::SelectionSource::Degraded, 1), 0.0);

  EXPECT_TRUE(hostSeen(*worker_lb, healthy_host, 200));
  EXPECT_TRUE(hostSeen(*worker_lb, remote_host, 400));
}

TEST_F(LoadAwareLocalityLbTest, PanicUsesAllHosts) {
  const auto hosts = setupPanicHosts();
  createLb();

  // Only 1 of 3 hosts healthy → live LoadBalancerBase panic on priority 0; all hosts eligible.
  expectWeights(PriorityRoutingWeights::SelectionSource::AllHosts, {1.0, 2.0});

  auto worker_lb = createWorkerLb();
  auto counts = countPicks(*worker_lb, 400);
  EXPECT_GT(counts[hosts.healthy.get()], 100);
  EXPECT_GT(counts[hosts.unhealthy1.get()] + counts[hosts.unhealthy2.get()], 200);
  EXPECT_GT(cluster_info_.lbStats().lb_healthy_panic_.value(), 0u);

  // peekAnotherHost resolves panic through the same path but must not double-count the stat.
  const uint64_t panic_count = cluster_info_.lbStats().lb_healthy_panic_.value();
  worker_lb->peekAnotherHost(nullptr);
  EXPECT_EQ(panic_count, cluster_info_.lbStats().lb_healthy_panic_.value());
}

TEST_F(LoadAwareLocalityLbTest, MembershipAndTopologyUpdatesShareTheSameWorkerLb) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = createWorkerLb();
  EXPECT_TRUE(hostSeen(*worker_lb, h1, 100));
  EXPECT_TRUE(hostSeen(*worker_lb, h2, 100));

  auto h3 = makeWeightTrackingMockHost();
  reshapeLocalities(0, {{h1}, {h2, h3}}, /*added=*/{h3});
  EXPECT_TRUE(hostSeen(*worker_lb, h3, 200));

  reshapeLocalities(0, {{h1}, {h3}}, /*added=*/{}, /*removed=*/{h2});
  EXPECT_FALSE(hostSeen(*worker_lb, h2, 200));
  EXPECT_TRUE(hostSeen(*worker_lb, h3, 200));

  auto h4 = makeWeightTrackingMockHost();
  reshapeLocalities(0, {{h1}, {h3}, {h4}}, /*added=*/{h4});

  publishHealthyWeights({1.0, 1.0, 1.0});
  EXPECT_TRUE(hostSeen(*worker_lb, h4, 300));
}

TEST_F(LoadAwareLocalityLbTest, EmptyDeltaUpdatesRefreshChildWeights) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1, h2}});

  createLb();
  auto worker_lb = createWorkerLb();

  auto initial_counts = countPicks(*worker_lb, 200);
  EXPECT_GT(initial_counts[h1.get()], 50);
  EXPECT_GT(initial_counts[h2.get()], 50);

  h1->weight(100);
  priority_set_.runUpdateCallbacks(0, {}, {});

  auto updated_counts = countPicks(*worker_lb, 400);
  EXPECT_GT(updated_counts[h1.get()], updated_counts[h2.get()] * 10);
  EXPECT_GT(updated_counts[h1.get()], 300);
}

TEST_F(LoadAwareLocalityLbTest, FailoverIsCallbackFreshNotTimerGated) {
  auto h_p0 = makeWeightTrackingMockHost();
  auto h_p1 = makeWeightTrackingMockHost();

  setupPriorityLocalities(0, {{h_p0}});
  setupPriorityLocalities(1, {{h_p1}});

  createLb();
  auto worker_lb = createWorkerLb();

  // Priority 0 is healthy, so live failover keeps load on it.
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(h_p0, worker_lb->chooseHost(nullptr).host);
  }

  // Flip priority 0 to all-unhealthy (keep hosts_, clear healthy_hosts_) and fire the membership
  // update callback ONLY. The weight-update timer is NOT invoked and simulated time is NOT
  // advanced past weight_update_period, so no new routing-weight snapshot is published.
  reshapeLocalities(0, /*localities=*/{}, /*added=*/{}, /*removed=*/{}, /*has_local=*/false,
                    std::vector<Upstream::HostVector>{{}});

  // Failover to priority 1 happens immediately from the live LoadBalancerBase callback, proving it
  // is not gated on a timer-published weight snapshot.
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(h_p1, worker_lb->chooseHost(nullptr).host);
  }

  reshapeLocalities(0, /*localities=*/{}, /*added=*/{}, /*removed=*/{}, /*has_local=*/false,
                    std::vector<Upstream::HostVector>{{h_p0}});
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(h_p0, worker_lb->chooseHost(nullptr).host);
  }
}

// Advisory weights are keyed by Locality identity, so adding a locality that shifts another's
// lexicographic index between the snapshot tick and the worker's live membership must NOT misplace
// the weight. Set up local A + remote C with distinct weights, publish a snapshot, then add remote
// B (sorts between A and C, shifting C from index 1 to index 2) WITHOUT recomputing weights, and
// assert C's traffic still follows C's weight (and the new B, absent from the snapshot, gets none).
TEST_F(LoadAwareLocalityLbTest, AdvisoryWeightsFollowLocalityIdentityAcrossIndexShift) {
  const auto locality_a = Upstream::Locality("a", "z", "sz"); // local, index 0
  const auto locality_b = Upstream::Locality("b", "z", "sz"); // sorts between A and C
  const auto locality_c =
      Upstream::Locality("c", "z", "sz"); // index 1, shifts to 2 when B is added

  auto host_a = makeWeightTrackingMockHost();
  auto host_c = makeWeightTrackingMockHost();
  setHostLocalityExplicit(host_a, locality_a);
  setHostLocalityExplicit(host_c, locality_c);

  auto* host_set = priority_set_.getMockHostSet(0);
  host_set->hosts_ = {host_a, host_c};
  host_set->healthy_hosts_ = {host_a, host_c};
  host_set->hosts_per_locality_ = Upstream::makeHostsPerLocality({{host_a}, {host_c}});
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;

  createLb();
  auto worker_lb = createWorkerLb();

  // Snapshot computed for the {A, C} ordering: A gets almost no weight, C gets all of it.
  PriorityRoutingWeights weights;
  weights.by_source[0].weights[locality_a] = 0.0;
  weights.by_source[0].weights[locality_c] = 1.0;
  publishSnapshot(makeSnapshot({weights}));
  expectOnlyHost(*worker_lb, host_c, 50);

  // Add remote B, whose identity sorts between A and C, so C's index shifts from 1 to 2. The
  // snapshot is NOT recomputed: it still only knows A and C by identity.
  auto host_b = makeWeightTrackingMockHost();
  setHostLocalityExplicit(host_b, locality_b);
  host_set->hosts_ = {host_a, host_b, host_c};
  host_set->healthy_hosts_ = {host_a, host_b, host_c};
  host_set->hosts_per_locality_ = Upstream::makeHostsPerLocality({{host_a}, {host_b}, {host_c}});
  host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;
  priority_set_.runUpdateCallbacks(0, {host_b}, {});

  // Identity mapping survives the index shift: C still carries all the weight (its weight was NOT
  // applied to B at the now-stale index 1), and B — absent from the snapshot — receives nothing.
  expectOnlyHost(*worker_lb, host_c, 50);
  EXPECT_FALSE(hostSeen(*worker_lb, host_b, 200));
  EXPECT_FALSE(hostSeen(*worker_lb, host_a, 200));
}

TEST_F(LoadAwareLocalityLbTest, DedicatedStatsAllOverloadedTotalIncOncePerTickWhenAllZeroHeadroom) {
  auto h_a = makeHosts(2);
  auto h_b = makeHosts(1);
  // With a local locality present, the fallback must still win over the local-preference snap.
  setupLocalities({h_a, h_b}, /*has_local_locality=*/true);

  createLb();
  EXPECT_EQ(0, counterValue(cluster_info_, "load_aware_locality.all_overloaded_total"));

  setUtilizationForHosts(h_a, 1.0);
  setUtilizationForHosts(h_b, 1.0);
  recomputeWeights();
  EXPECT_EQ(1, counterValue(cluster_info_, "load_aware_locality.all_overloaded_total"));

  expectCounterIncrementsPerTick("load_aware_locality.all_overloaded_total", 1);
}

TEST_F(LoadAwareLocalityLbTest, DedicatedStatsLocalPreferredTotalIncOncePerTickOnVarianceSnap) {
  expectTwoHostSnapCounter("load_aware_locality.local_preferred_total",
                           /*variance_threshold=*/0.0, /*remote_probe_fraction=*/0.0,
                           std::make_pair(0.9, 0.1), std::make_pair(0.1, 0.9));
}

TEST_F(LoadAwareLocalityLbTest, DedicatedStatsProbeActiveTotalIncOncePerTickWhenProbeKicksIn) {
  expectTwoHostSnapCounter("load_aware_locality.probe_active_total",
                           /*variance_threshold=*/1.0, /*remote_probe_fraction=*/0.05, std::nullopt,
                           std::make_pair(0.3, 1.0));
}

TEST_F(LoadAwareLocalityLbTest, DedicatedStatsSpillActiveTotalIncOncePerTickWhenSpilling) {
  // Startup no-data tick snaps local, so the spill counter starts at 0; the balanced quiet phase
  // also snaps; only the overloaded-local phase spills.
  expectTwoHostSnapCounter("load_aware_locality.spill_active_total",
                           /*variance_threshold=*/0.1, /*remote_probe_fraction=*/0.0,
                           std::make_pair(0.2, 0.2), std::make_pair(0.9, 0.2),
                           /*startup_count=*/0);
}

TEST_F(LoadAwareLocalityLbTest, DedicatedStatsSpillActiveNotCountedWithoutEligibleLocalHosts) {
  auto h_local = makeHosts(1);
  auto h_remote = makeHosts(1);
  // Local hosts are ineligible in the healthy view: spill requires the utilization comparison to
  // have run and failed; an empty local slice spills for health, not load.
  setupLocalities({h_local, h_remote}, /*has_local_locality=*/true,
                  std::vector<Upstream::HostVector>{{}, h_remote});

  createLb();
  setUtilizationForHosts(h_remote, 0.2);
  recomputeWeights();
  EXPECT_EQ(0, counterValue(cluster_info_, "load_aware_locality.spill_active_total"));
  expectCounterIncrementsPerTick("load_aware_locality.spill_active_total", 0);
}

TEST_F(LoadAwareLocalityLbTest, DedicatedStatsStaleLocalityTotalCountsOncePerStalePerTick) {
  auto locality_a = makeHosts(2);
  auto locality_b = makeHosts(1);
  auto locality_c = makeHosts(3);
  setupLocalities({locality_a, locality_b, locality_c});

  createLb(/*variance_threshold=*/0.1, /*ewma_alpha=*/1.0, /*remote_probe_fraction=*/0.0,
           std::chrono::milliseconds(5000));

  EXPECT_EQ(0, counterValue(cluster_info_, "load_aware_locality.stale_locality_total"));
  expectCounterIncrementsPerTick("load_aware_locality.stale_locality_total", 0);

  setUtilizationForHosts(locality_a, 0.5);
  setUtilizationForHosts(locality_b, 0.4);
  setUtilizationForHosts(locality_c, 0.6);
  recomputeWeights();
  const uint64_t baseline = counterValue(cluster_info_, "load_aware_locality.stale_locality_total");

  context_.time_system_.advanceTimeWait(std::chrono::milliseconds(6000));
  setUtilizationForHosts(locality_a, 0.5);
  recomputeWeights();
  EXPECT_EQ(baseline + 2, counterValue(cluster_info_, "load_aware_locality.stale_locality_total"));

  expectCounterIncrementsPerTick("load_aware_locality.stale_locality_total", 2);
}

TEST_F(LoadAwareLocalityLbTest, WeightUpdateTimerRearmsOnEachTick) {
  auto h = makeWeightTrackingMockHost();
  setupLocalities({{h}});
  createLb();
  // initialize() ran the first tick, which re-arms before computing.
  ASSERT_TRUE(timer_->enabled_);
  // Every fired tick must re-arm; a conditional or late re-arm would leave the timer disabled.
  timer_->invokeCallback();
  EXPECT_TRUE(timer_->enabled_);
  timer_->invokeCallback();
  EXPECT_TRUE(timer_->enabled_);
}

TEST_F(LoadAwareLocalityLbTest, ChildContextForwardsMethodsExceptRetryPriorityAndAsyncCallback) {
  auto h = makeWeightTrackingMockHost();
  setupLocalities({{h}});

  int inspected = 0;
  Upstream::PrioritySetImpl child_priority_set;
  auto child_factory = std::make_shared<RecordingWorkerChildFactory>(
      /*recreate_on_host_change=*/false,
      [&child_priority_set, h, &inspected](Upstream::LoadBalancerContext& context) {
        ++inspected;
        std::ignore = context.computeHashKey();
        const Upstream::HealthyAndDegradedLoad default_priority_load{Upstream::HealthyLoad({100}),
                                                                     Upstream::DegradedLoad({0})};
        EXPECT_EQ(&default_priority_load,
                  &context.determinePriorityLoad(child_priority_set, default_priority_load,
                                                 Upstream::RetryPriority::defaultPriorityMapping));
        std::ignore = context.metadataMatchCriteria();
        std::ignore = context.downstreamConnection();
        std::ignore = context.requestStreamInfo();
        std::ignore = context.downstreamHeaders();
        std::ignore = context.shouldSelectAnotherHost(*h);
        std::ignore = context.hostSelectionRetryCount();
        std::ignore = context.upstreamSocketOptions();
        std::ignore = context.upstreamTransportSocketOptions();
        std::ignore = context.overrideHostToSelect();
        Upstream::HostConstSharedPtr async_host;
        context.onAsyncHostSelection(std::move(async_host), "ignored");
        context.setHeadersModifier(nullptr);
      });
  auto factory = makeWorkerFactoryWithChild(child_factory);
  auto worker_lb = factory->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  NiceMock<Upstream::MockLoadBalancerContext> ctx;
  // The parent consults retry-priority once per selection. The child wrapper returns its default
  // priority load locally and never forwards the child's single-priority view to the parent.
  EXPECT_CALL(ctx, determinePriorityLoad(testing::Ref(priority_set_), testing::_, testing::_))
      .Times(2)
      .WillRepeatedly(
          [](const Upstream::PrioritySet&,
             const Upstream::HealthyAndDegradedLoad& default_priority_load,
             const Upstream::RetryPriority::PriorityMappingFunc&)
              -> const Upstream::HealthyAndDegradedLoad& { return default_priority_load; });
  EXPECT_CALL(ctx, onAsyncHostSelection(testing::_, testing::_)).Times(0);

  EXPECT_EQ(h, worker_lb->chooseHost(&ctx).host);
  EXPECT_EQ(h, worker_lb->peekAnotherHost(&ctx));
  EXPECT_EQ(2, inspected);
}

// An asynchronous child LB would retain the stack-scoped child context and call back after
// chooseHost returns (use-after-free). The policy must cancel the async selection and fail
// synchronously, never propagating the cancellation handle to its caller.
TEST_F(LoadAwareLocalityLbTest, AsyncChildSelectionIsCancelledAndFailsSynchronously) {
  auto h = makeWeightTrackingMockHost();
  setupLocalities({{h}});

  bool cancelled = false;
  auto child_factory = std::make_shared<AsyncSelectingChildFactory>(cancelled);
  auto factory = makeWorkerFactoryWithChild(child_factory);
  auto worker_lb = factory->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);

  NiceMock<Upstream::MockLoadBalancerContext> ctx;
  auto response = worker_lb->chooseHost(&ctx);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ(nullptr, response.cancelable);
  EXPECT_TRUE(cancelled);
}

TEST_F(LoadAwareLocalityLbTest, PeekAndChooseReplayTheSameRandomSequence) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});
  createLb();
  auto worker_lb = createWorkerLb();

  publishHealthyWeights({0.0, 1.0});
  auto counts = countPicks(*worker_lb, 100);
  EXPECT_GE(counts[h2.get()], 98);

  publishHealthyWeights({0.1, 0.9});
  forceRandomValues({0});
  auto peeked = worker_lb->peekAnotherHost(nullptr);
  ASSERT_NE(nullptr, peeked);

  // The choose must replay the peek's stashed draw, leaving this decoy value unconsumed.
  forceRandomValues({std::numeric_limits<uint64_t>::max() - 1});
  auto chosen = worker_lb->chooseHost(nullptr).host;
  EXPECT_EQ(peeked.get(), chosen.get());
}

TEST_F(LoadAwareLocalityLbTest, WorkerFallbackEdgeCases) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = createWorkerLb();

  // splitMix64(3)/max ~= 0.11 -> locality 0 under the equal host-count fallback weights.
  forceRandomValues({3});
  EXPECT_EQ(h1, worker_lb->chooseHost(nullptr).host);

  publishSnapshot(makeSnapshot({PriorityRoutingWeights{}}));
  EXPECT_EQ(h1, worker_lb->chooseHost(nullptr).host);

  publishHealthyWeights({0.0, 0.0});
  EXPECT_EQ(h1, worker_lb->chooseHost(nullptr).host);

  publishHealthyWeights({1.0, 1.0});
  // splitMix64(6)/max ~= 0.74, in the upper half of the equal-weight distribution -> locality 1.
  forceRandomValues({6});
  EXPECT_EQ(h2, worker_lb->chooseHost(nullptr).host);

  worker_lb = createWorkerLb();
  Upstream::HealthyAndDegradedLoad degraded_load{Upstream::HealthyLoad({0}),
                                                 Upstream::DegradedLoad({100})};
  NiceMock<Upstream::MockLoadBalancerContext> ctx;
  EXPECT_CALL(ctx, determinePriorityLoad(testing::Ref(priority_set_), testing::_, testing::_))
      .WillOnce(ReturnRef(degraded_load));
  EXPECT_EQ(nullptr, worker_lb->peekAnotherHost(&ctx));

  publishHealthyWeights({0.0, 1.0});
  EXPECT_EQ(h2, worker_lb->chooseHost(nullptr).host);

  reshapeLocalities(0, {{h1}, {}}, /*added=*/{}, /*removed=*/{h2});
  EXPECT_EQ(h1, worker_lb->chooseHost(nullptr).host);

  reshapeLocalities(0, {{}, {}}, /*added=*/{}, /*removed=*/{h1});
  EXPECT_EQ(nullptr, worker_lb->chooseHost(nullptr).host);

  auto h_p1 = makeWeightTrackingMockHost();
  setupPriorityLocalities(1, {{h_p1}});
  priority_set_.runUpdateCallbacks(1, {}, {});
  Upstream::HealthyAndDegradedLoad force_priority_1{Upstream::HealthyLoad({0, 100}),
                                                    Upstream::DegradedLoad({0, 0})};
  NiceMock<Upstream::MockLoadBalancerContext> priority_ctx;
  EXPECT_CALL(priority_ctx,
              determinePriorityLoad(testing::Ref(priority_set_), testing::_, testing::_))
      .WillOnce(ReturnRef(force_priority_1));
  EXPECT_EQ(nullptr, worker_lb->chooseHost(&priority_ctx).host);
}

TEST_F(LoadAwareLocalityLbTest, PeekAnotherHostCapsPreconnectLookahead) {
  auto h = makeWeightTrackingMockHost();
  setupLocalities({{h}});

  createLb();
  auto worker_lb = createWorkerLb();

  EXPECT_EQ(h, worker_lb->peekAnotherHost(nullptr));
  EXPECT_EQ(nullptr, worker_lb->peekAnotherHost(nullptr));
}

TEST_F(LoadAwareLocalityLbTest, PeekLookaheadCountsPeeksNotDraws) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}});

  createLb();
  auto worker_lb = createWorkerLb();
  publishHealthyWeights({1.0, 1.0});

  // Multi-locality picks make both a priority and a locality decision from one stashed draw, so
  // two healthy hosts allow exactly two peeks. Forced hashes route the peeks to distinct
  // localities (splitMix64 fractions 0.11 and 0.74) so the per-locality child caps don't gate.
  forceRandomValues({3, 6});
  EXPECT_EQ(h1, worker_lb->peekAnotherHost(nullptr));
  EXPECT_EQ(h2, worker_lb->peekAnotherHost(nullptr));
  EXPECT_EQ(nullptr, worker_lb->peekAnotherHost(nullptr));
}

TEST_F(LoadAwareLocalityLbTest, RecomputeTotalIncrementsEveryTick) {
  auto h = makeWeightTrackingMockHost();
  setupLocalities({{h}});

  createLb();
  EXPECT_EQ(1, counterValue(cluster_info_, "load_aware_locality.recompute_total"));
  expectCounterIncrementsPerTick("load_aware_locality.recompute_total", 1);
  expectCounterIncrementsPerTick("load_aware_locality.recompute_total", 1);
}

TEST_F(LoadAwareLocalityLbTest, ZoneRoutingStatsCoverHealthyRoutingPaths) {
  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);

  createLb(/*variance_threshold=*/1.0, /*ewma_alpha=*/1.0, /*remote_probe_fraction=*/0.0);
  setHostUtilization(h_local, 0.3);
  setHostUtilization(h_remote, 0.3);
  recomputeWeights();

  auto worker_lb = createWorkerLb();
  expectOnlyHost(*worker_lb, h_local, 10);
  EXPECT_EQ(10, static_cast<int>(cluster_info_.lbStats().lb_zone_routing_all_directly_.value()));

  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);
  createLb(/*variance_threshold=*/0.0, /*ewma_alpha=*/1.0, /*remote_probe_fraction=*/0.0);
  setHostUtilization(h_local, 0.31);
  setHostUtilization(h_remote, 0.3);
  recomputeWeights();

  worker_lb = createWorkerLb();
  for (int i = 0; i < 200; ++i) {
    worker_lb->chooseHost(nullptr);
  }
  EXPECT_GT(cluster_info_.lbStats().lb_zone_routing_sampled_.value(), 0);
  EXPECT_GT(cluster_info_.lbStats().lb_zone_routing_cross_zone_.value(), 0);
}

TEST_F(LoadAwareLocalityLbTest, ZoneRoutingStatsProbeCountsLocalPicksAsSampled) {
  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}}, /*has_local_locality=*/true);

  // Local preference snaps to all-local, then the probe redistribution diverts traffic: local
  // picks are sampled from a <100% local distribution, never all-directly.
  createLb(/*variance_threshold=*/1.0, /*ewma_alpha=*/1.0, /*remote_probe_fraction=*/0.03);
  setHostUtilization(h_local, 0.3);
  setHostUtilization(h_remote, 0.3);
  recomputeWeights();
  expectAllLocal(false);

  auto worker_lb = createWorkerLb();
  for (int i = 0; i < 200; ++i) {
    worker_lb->chooseHost(nullptr);
  }
  EXPECT_EQ(0, static_cast<int>(cluster_info_.lbStats().lb_zone_routing_all_directly_.value()));
  EXPECT_GT(cluster_info_.lbStats().lb_zone_routing_sampled_.value(), 0);
  EXPECT_GT(cluster_info_.lbStats().lb_zone_routing_cross_zone_.value(), 0);
}

TEST_F(LoadAwareLocalityLbTest, ZoneRoutingStatsFollowWorkerTopologyOnLocalityFlip) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}}, /*has_local_locality=*/false);

  createLb();
  setHostUtilization(h1, 0.3);
  setHostUtilization(h2, 0.3);
  recomputeWeights();

  auto worker_lb = createWorkerLb();
  for (int i = 0; i < 100; ++i) {
    worker_lb->chooseHost(nullptr);
  }
  EXPECT_EQ(0u, zoneRoutingStatTotal());

  // The local locality appears on this worker before the next snapshot: attribution follows the
  // worker's live topology immediately rather than the stale snapshot.
  reshapeLocalities(0, {{h1}, {h2}}, /*added=*/{}, /*removed=*/{}, /*has_local=*/true);
  for (int i = 0; i < 100; ++i) {
    worker_lb->chooseHost(nullptr);
  }
  EXPECT_GT(zoneRoutingStatTotal(), 0u);
}

TEST_F(LoadAwareLocalityLbTest, ZoneRoutingStatsCoverNoLocalDegradedAndPanicSources) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2 = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2}}, /*has_local_locality=*/false);

  createLb();
  setHostUtilization(h1, 0.3);
  setHostUtilization(h2, 0.3);
  recomputeWeights();

  auto worker_lb = createWorkerLb();
  for (int i = 0; i < 100; ++i) {
    worker_lb->chooseHost(nullptr);
  }
  EXPECT_EQ(0u, zoneRoutingStatTotal());

  auto h_local = makeWeightTrackingMockHost();
  auto h_remote = makeWeightTrackingMockHost();
  setupLocalities({{h_local}, {h_remote}},
                  /*has_local_locality=*/true, std::vector<Upstream::HostVector>{{}, {}},
                  std::vector<Upstream::HostVector>{{h_local}, {h_remote}});

  createLb();
  // Degraded-source spill: local degraded utilization above the remote average, so local picks
  // count as sampled and remote picks as cross-zone.
  setHostUtilization(h_local, 0.9);
  setHostUtilization(h_remote, 0.2);
  recomputeWeights();

  worker_lb = createWorkerLb();
  auto degraded_counts = countPicks(*worker_lb, 400);
  EXPECT_GT(degraded_counts[h_local.get()], 0);
  EXPECT_GT(degraded_counts[h_remote.get()], 0);
  EXPECT_GT(cluster_info_.lbStats().lb_zone_routing_sampled_.value(), 0);
  EXPECT_GT(cluster_info_.lbStats().lb_zone_routing_cross_zone_.value(), 0);

  const uint64_t before_panic_zone_stats = zoneRoutingStatTotal();
  const auto panic_hosts = setupPanicHosts(/*has_local_locality=*/true);

  createLb();
  recomputeWeights();
  worker_lb = createWorkerLb();
  auto panic_counts = countPicks(*worker_lb, 400);
  EXPECT_GT(panic_counts[panic_hosts.healthy.get()] + panic_counts[panic_hosts.unhealthy1.get()] +
                panic_counts[panic_hosts.unhealthy2.get()],
            0);
  EXPECT_GT(cluster_info_.lbStats().lb_healthy_panic_.value(), 0u);
  EXPECT_EQ(before_panic_zone_stats, zoneRoutingStatTotal());
}

TEST_F(LoadAwareLocalityLbTest, InitializePropagatesChildFactoryError) {
  NiceMock<Upstream::MockTypedLoadBalancerFactory> null_child_factory;
  ON_CALL(null_child_factory, name()).WillByDefault(Return("mock_null_factory"));
  ON_CALL(null_child_factory,
          create(testing::_, testing::_, testing::_, testing::_, testing::_, testing::_))
      .WillByDefault(testing::Return(testing::ByMove(nullptr)));

  auto lb_config = std::make_unique<LoadAwareLocalityLbConfig>(
      null_child_factory, nullptr, std::chrono::milliseconds(1000), 0.1, 0.3, 0.03,
      std::chrono::milliseconds(180000), std::vector<std::string>{}, dispatcher_,
      context_.thread_local_);

  LoadAwareLocalityLoadBalancer lb(*lb_config, cluster_info_, priority_set_,
                                   context_.runtime_loader_, random_, context_.time_system_);
  auto status = lb.initialize();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_THAT(status.message(), testing::HasSubstr("mock_null_factory"));
}

TEST_F(LoadAwareLocalityLbTest, ChildRecreatedWhenUnderlyingFactoryRequiresIt) {
  auto local = makeWeightTrackingMockHost();
  auto remote = makeWeightTrackingMockHost();
  setupLocalities({{local}, {remote}});

  auto child_factory = std::make_shared<RecordingWorkerChildFactory>(true);
  auto factory = makeWorkerFactoryWithChild(child_factory);

  auto worker_lb = factory->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);
  const uint32_t initial_create_count = child_factory->createCount();
  EXPECT_GT(initial_create_count, 0);

  auto extra = makeWeightTrackingMockHost();
  reshapeLocalities(0, {{local}, {remote, extra}}, /*added=*/{extra});
  EXPECT_GT(child_factory->createCount(), initial_create_count);
}

TEST_F(LoadAwareLocalityLbTest, AddedPriorityBuildsOnlyItsOwnChildLbs) {
  auto local = makeWeightTrackingMockHost();
  auto remote = makeWeightTrackingMockHost();
  setupLocalities({{local}, {remote}});

  auto child_factory = std::make_shared<RecordingWorkerChildFactory>(false);
  auto factory = makeWorkerFactoryWithChild(child_factory);
  auto worker_lb = factory->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);
  // P0: two localities, each with healthy and all-hosts children.
  const uint32_t p0_create_count = child_factory->createCount();
  EXPECT_EQ(p0_create_count, 4);

  auto p1_host = makeWeightTrackingMockHost();
  setupPriorityLocalities(1, {{p1_host}});
  priority_set_.runUpdateCallbacks(1, {p1_host}, {});

  // Only the new priority's children are built; P0's live child LBs are preserved.
  EXPECT_EQ(child_factory->createCount(), p0_create_count + 2);
  auto picked = worker_lb->chooseHost(nullptr).host;
  EXPECT_TRUE(picked == local || picked == remote);
}

TEST_F(LoadAwareLocalityLbTest, ExistingPriorityDeltaSyncsWhenPriorityCountGrows) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2a = makeWeightTrackingMockHost();
  auto h2b = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2a, h2b}});

  auto child_factory = std::make_shared<RecordingWorkerChildFactory>(true);
  auto factory = makeWorkerFactoryWithChild(child_factory);
  auto worker_lb = factory->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);
  const uint32_t initial_create_count = child_factory->createCount();

  // Grow the priority set without a callback (as intermediate host-set creation does), then
  // deliver a P0 membership delta while the priority counts still disagree.
  setupPriorityLocalities(1, {{makeWeightTrackingMockHost()}});
  reshapeLocalities(0, {{h1}, {h2a}}, /*added=*/{}, /*removed=*/{h2b});

  // P1 is built (+2) and P0's own delta is not dropped: locality 1's healthy and all-hosts
  // children are recreated (+2) under the recreate-on-host-change contract.
  EXPECT_EQ(child_factory->createCount(), initial_create_count + 4);
}

TEST_F(LoadAwareLocalityLbTest, HealthOnlyUpdateSyncsExistingPriorityWhileGrowthPending) {
  auto h1 = makeWeightTrackingMockHost();
  auto h2a = makeWeightTrackingMockHost();
  auto h2b = makeWeightTrackingMockHost();
  setupLocalities({{h1}, {h2a, h2b}});

  auto child_factory = std::make_shared<RecordingWorkerChildFactory>(true);
  auto factory = makeWorkerFactoryWithChild(child_factory);
  auto worker_lb = factory->create({priority_set_, nullptr});
  ASSERT_NE(nullptr, worker_lb);
  const uint32_t initial_create_count = child_factory->createCount();

  // Grow the priority set without a callback, then deliver a P0 health-only (empty-delta) update
  // while the priority counts still disagree.
  setupPriorityLocalities(1, {{makeWeightTrackingMockHost()}});
  reshapeLocalities(0, /*localities=*/{}, /*added=*/{}, /*removed=*/{}, /*has_local=*/false,
                    std::vector<Upstream::HostVector>{{h1}, {h2a}});

  // P1 stays pending (an empty delta cannot rebuild), but P0's healthy flip is applied: locality
  // 2's healthy child is recreated under the recreate-on-host-change contract.
  EXPECT_EQ(child_factory->createCount(), initial_create_count + 1);
}

// Wraps metric names for LocalityLbHostData's shared_ptr constructor.
std::shared_ptr<const std::vector<std::string>> makeMetricNames(std::vector<std::string> names) {
  return std::make_shared<const std::vector<std::string>>(std::move(names));
}

TEST_F(LoadAwareLocalityLbTest, MetricNamesDriveUtilization) {
  // LocalityLbHostData with metric_names={"named_metrics.foo"} delegates to
  // OrcaLoadReportHandler::getUtilizationFromOrcaReport, which follows the runtime-feature
  // controlled precedence: named_metrics → application_utilization → cpu_utilization
  // (when the default orca_weight_manager_use_named_metrics_first flag is enabled).
  struct Case {
    std::string name;
    std::vector<std::string> metric_names;
    std::optional<double> named_metric;
    std::optional<double> application_utilization;
    std::optional<double> cpu_utilization;
    double expected_utilization;
  };

  const std::vector<Case> cases = {
      {"NamedMetric", {"named_metrics.foo"}, 0.6, std::nullopt, std::nullopt, 0.6},
      {"NamedMetricAbsentFallsBackToCpu",
       {"named_metrics.foo"},
       std::nullopt,
       std::nullopt,
       0.3,
       0.3},
      {"ApplicationUtilization", {}, std::nullopt, 0.7, std::nullopt, 0.7},
      {"CpuFallback", {}, std::nullopt, std::nullopt, 0.4, 0.4},
  };

  for (const Case& c : cases) {
    SCOPED_TRACE(c.name);
    LocalityLbHostData slot(simTime(), makeMetricNames(c.metric_names));
    xds::data::orca::v3::OrcaLoadReport report;
    if (c.named_metric.has_value()) {
      (*report.mutable_named_metrics())["foo"] = *c.named_metric;
    }
    if (c.application_utilization.has_value()) {
      report.set_application_utilization(*c.application_utilization);
    }
    if (c.cpu_utilization.has_value()) {
      report.set_cpu_utilization(*c.cpu_utilization);
    }
    ASSERT_TRUE(slot.onOrcaLoadReport(report, stream_info_).ok());
    EXPECT_DOUBLE_EQ(c.expected_utilization, slot.utilization());
  }
}

// When getUtilizationFromOrcaReport returns a non-finite value (Inf/NaN), storeUtilization
// discards it: utilization() stays 0.0 and lastUpdateTime() stays kNeverReported.
TEST_F(LoadAwareLocalityLbTest, StoreUtilizationRejectsNonFiniteValue) {
  LocalityLbHostData slot(simTime(), makeMetricNames({}));

  // cpu_utilization is the last-resort fallback when no metric names are configured and
  // application_utilization is 0. Setting it to Inf produces a non-finite util value.
  xds::data::orca::v3::OrcaLoadReport report;
  report.set_cpu_utilization(std::numeric_limits<double>::infinity());
  ASSERT_TRUE(slot.onOrcaLoadReport(report, stream_info_).ok());

  // Non-finite value rejected: slot remains at its initial state.
  EXPECT_DOUBLE_EQ(0.0, slot.utilization());
  EXPECT_EQ(LocalityLbHostData::kNeverReported, slot.lastUpdateTime());
}

} // namespace
} // namespace LoadAwareLocality
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
