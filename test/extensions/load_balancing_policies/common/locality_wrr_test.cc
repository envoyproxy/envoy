#include "envoy/upstream/upstream.h"

#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/load_balancing_policies/common/locality_wrr.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

class LocalityWrrTest : public Event::TestUsingSimulatedTime, public ::testing::Test {
public:
  LocalityWrrTest() {
    host_set_ = std::make_unique<HostSetImpl>(0, false, kDefaultOverProvisioningFactor);
  }

  absl::optional<uint32_t> chooseDegradedLocality() {
    return locality_wrr_->chooseDegradedLocality();
  }

  absl::optional<uint32_t> chooseHealthyLocality() {
    return locality_wrr_->chooseHealthyLocality();
  }

  std::unique_ptr<HostSetImpl> host_set_;
  std::unique_ptr<LocalityWrr> locality_wrr_ = nullptr;
  std::shared_ptr<MockClusterInfo> info_{new NiceMock<MockClusterInfo>()};
};

TEST_F(LocalityWrrTest, HostSetEmpty) {
  locality_wrr_ = std::make_unique<LocalityWrr>(*host_set_, 0);

  EXPECT_EQ(chooseHealthyLocality(), absl::nullopt);
  EXPECT_EQ(chooseDegradedLocality(), absl::nullopt);
}

TEST_F(LocalityWrrTest, AllHostsUnhealthy) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  envoy::config::core::v3::Locality zone_c;
  zone_c.set_zone("C");
  HostVector hosts{makeTestHost(info_, "tcp://127.0.0.1:80", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:81", zone_b),
                   makeTestHost(info_, "tcp://127.0.0.1:82", zone_c)};

  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{hosts[0]}, {hosts[1]}, {hosts[2]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 1, 1}};
  auto hosts_const_shared = std::make_shared<const HostVector>(hosts);
  host_set_->updateHosts(updateHostsParams(hosts_const_shared, hosts_per_locality),
                         locality_weights, {}, {}, absl::nullopt);
  locality_wrr_ = std::make_unique<LocalityWrr>(*host_set_, 0);

  EXPECT_FALSE(chooseHealthyLocality().has_value());
}

// When a locality has endpoints that have not yet been warmed, weight calculation should ignore
// these hosts.
TEST_F(LocalityWrrTest, NotWarmedHostsLocality) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  HostVector hosts{makeTestHost(info_, "tcp://127.0.0.1:80", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:81", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:82", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:83", zone_b),
                   makeTestHost(info_, "tcp://127.0.0.1:84", zone_b)};

  // We have two localities with 3 hosts in A, 2 hosts in B. Two of the hosts in A are not
  // warmed yet, so even though they are unhealthy we should not adjust the locality weight.
  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{hosts[0], hosts[1], hosts[2]}, {hosts[3], hosts[4]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 1}};
  auto hosts_const_shared = std::make_shared<const HostVector>(hosts);
  HostsPerLocalitySharedPtr healthy_hosts_per_locality =
      makeHostsPerLocality({{hosts[0]}, {hosts[3], hosts[4]}});
  HostsPerLocalitySharedPtr excluded_hosts_per_locality =
      makeHostsPerLocality({{hosts[1], hosts[2]}, {}});

  host_set_->updateHosts(
      HostSetImpl::updateHostsParams(
          hosts_const_shared, hosts_per_locality,
          makeHostsFromHostsPerLocality<HealthyHostVector>(healthy_hosts_per_locality),
          healthy_hosts_per_locality, std::make_shared<const DegradedHostVector>(),
          HostsPerLocalityImpl::empty(),
          makeHostsFromHostsPerLocality<ExcludedHostVector>(excluded_hosts_per_locality),
          excluded_hosts_per_locality),
      locality_weights, {}, {}, absl::nullopt);
  locality_wrr_ = std::make_unique<LocalityWrr>(*host_set_, 0);

  // We should RR between localities with equal weight.
  EXPECT_EQ(0, chooseHealthyLocality().value());
  EXPECT_EQ(1, chooseHealthyLocality().value());
  EXPECT_EQ(0, chooseHealthyLocality().value());
  EXPECT_EQ(1, chooseHealthyLocality().value());
}

TEST_F(LocalityWrrTest, AllZeroWeights) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  HostVector hosts{makeTestHost(info_, "tcp://127.0.0.1:80", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:81", zone_b)};

  HostsPerLocalitySharedPtr hosts_per_locality = makeHostsPerLocality({{hosts[0]}, {hosts[1]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{0, 0}};
  auto hosts_const_shared = std::make_shared<const HostVector>(hosts);
  host_set_->updateHosts(updateHostsParams(hosts_const_shared, hosts_per_locality,
                                           std::make_shared<const HealthyHostVector>(hosts),
                                           hosts_per_locality),
                         locality_weights, {}, {}, 0);
  locality_wrr_ = std::make_unique<LocalityWrr>(*host_set_, 0);

  EXPECT_FALSE(chooseHealthyLocality().has_value());
}

TEST_F(LocalityWrrTest, UnweightedLocalities) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  envoy::config::core::v3::Locality zone_c;
  zone_c.set_zone("C");
  HostVector hosts{makeTestHost(info_, "tcp://127.0.0.1:80", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:81", zone_b),
                   makeTestHost(info_, "tcp://127.0.0.1:82", zone_c)};

  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{hosts[0]}, {hosts[1]}, {hosts[2]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 1, 1}};
  auto hosts_const_shared = std::make_shared<const HostVector>(hosts);
  host_set_->updateHosts(updateHostsParams(hosts_const_shared, hosts_per_locality,
                                           std::make_shared<const HealthyHostVector>(hosts),
                                           hosts_per_locality),
                         locality_weights, {}, {}, absl::nullopt);

  locality_wrr_ = std::make_unique<LocalityWrr>(*host_set_, 0);

  EXPECT_EQ(0, chooseHealthyLocality().value());
  EXPECT_EQ(1, chooseHealthyLocality().value());
  EXPECT_EQ(2, chooseHealthyLocality().value());
  EXPECT_EQ(0, chooseHealthyLocality().value());
  EXPECT_EQ(1, chooseHealthyLocality().value());
  EXPECT_EQ(2, chooseHealthyLocality().value());
}

// When locality weights differ, we have weighted RR behavior.
TEST_F(LocalityWrrTest, WeightedLocalities) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  HostVector hosts{makeTestHost(info_, "tcp://127.0.0.1:80", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:81", zone_b)};

  HostsPerLocalitySharedPtr hosts_per_locality = makeHostsPerLocality({{hosts[0]}, {hosts[1]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 2}};
  auto hosts_const_shared = std::make_shared<const HostVector>(hosts);
  host_set_->updateHosts(updateHostsParams(hosts_const_shared, hosts_per_locality,
                                           std::make_shared<const HealthyHostVector>(hosts),
                                           hosts_per_locality),
                         locality_weights, {}, {}, absl::nullopt);

  locality_wrr_ = std::make_unique<LocalityWrr>(*host_set_, 0);

  EXPECT_EQ(1, chooseHealthyLocality().value());
  EXPECT_EQ(0, chooseHealthyLocality().value());
  EXPECT_EQ(1, chooseHealthyLocality().value());
  EXPECT_EQ(1, chooseHealthyLocality().value());
  EXPECT_EQ(0, chooseHealthyLocality().value());
  EXPECT_EQ(1, chooseHealthyLocality().value());
}
// Localities with no weight assignment are never picked.
TEST_F(LocalityWrrTest, MissingWeight) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  envoy::config::core::v3::Locality zone_c;
  zone_c.set_zone("C");
  HostVector hosts{makeTestHost(info_, "tcp://127.0.0.1:80", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:81", zone_b),
                   makeTestHost(info_, "tcp://127.0.0.1:82", zone_c)};

  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{hosts[0]}, {hosts[1]}, {hosts[2]}});
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 0, 1}};
  auto hosts_const_shared = std::make_shared<const HostVector>(hosts);
  host_set_->updateHosts(updateHostsParams(hosts_const_shared, hosts_per_locality,
                                           std::make_shared<const HealthyHostVector>(hosts),
                                           hosts_per_locality),
                         locality_weights, {}, {}, absl::nullopt);
  locality_wrr_ = std::make_unique<LocalityWrr>(*host_set_, 0);

  EXPECT_EQ(0, chooseHealthyLocality().value());
  EXPECT_EQ(2, chooseHealthyLocality().value());
  EXPECT_EQ(0, chooseHealthyLocality().value());
  EXPECT_EQ(2, chooseHealthyLocality().value());
  EXPECT_EQ(0, chooseHealthyLocality().value());
  EXPECT_EQ(2, chooseHealthyLocality().value());
}

// Validates that with weighted initialization all localities are chosen
// proportionally to their weight.
TEST_F(LocalityWrrTest, WeightedAllChosen) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  envoy::config::core::v3::Locality zone_c;
  zone_b.set_zone("C");
  HostVector hosts{makeTestHost(info_, "tcp://127.0.0.1:80", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:81", zone_b),
                   makeTestHost(info_, "tcp://127.0.0.1:82", zone_c)};

  HostsPerLocalitySharedPtr hosts_per_locality =
      makeHostsPerLocality({{hosts[0]}, {hosts[1]}, {hosts[2]}});
  // Set weights of 10%, 60% and 30% to the three zones.
  LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 6, 3}};

  // Keep track of how many times each locality is picked, initialized to 0.
  uint32_t locality_picked_count[] = {0, 0, 0};

  // Create the load-balancer 10 times, each with a different seed number (from
  // 0 to 10), do a single pick, and validate that the number of picks equals
  // to the weights assigned to the localities.
  auto hosts_const_shared = std::make_shared<const HostVector>(hosts);
  for (uint32_t i = 0; i < 10; ++i) {
    host_set_->updateHosts(updateHostsParams(hosts_const_shared, hosts_per_locality,
                                             std::make_shared<const HealthyHostVector>(hosts),
                                             hosts_per_locality),
                           locality_weights, {}, {}, i, absl::nullopt);
    locality_wrr_ = std::make_unique<LocalityWrr>(*host_set_, i);

    locality_picked_count[chooseHealthyLocality().value()]++;
  }
  EXPECT_EQ(locality_picked_count[0], 1);
  EXPECT_EQ(locality_picked_count[1], 6);
  EXPECT_EQ(locality_picked_count[2], 3);
}

// Gentle failover between localities as health diminishes.
TEST_F(LocalityWrrTest, UnhealthyFailover) {
  envoy::config::core::v3::Locality zone_a;
  zone_a.set_zone("A");
  envoy::config::core::v3::Locality zone_b;
  zone_b.set_zone("B");
  HostVector hosts{makeTestHost(info_, "tcp://127.0.0.1:80", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:81", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:82", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:83", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:84", zone_a),
                   makeTestHost(info_, "tcp://127.0.0.1:85", zone_b)};

  locality_wrr_ = std::make_unique<LocalityWrr>(*host_set_, 0);

  const auto setHealthyHostCount = [this, hosts](uint32_t host_count) {
    LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 2}};
    HostsPerLocalitySharedPtr hosts_per_locality =
        makeHostsPerLocality({{hosts[0], hosts[1], hosts[2], hosts[3], hosts[4]}, {hosts[5]}});
    HostVector healthy_hosts;
    for (uint32_t i = 0; i < host_count; ++i) {
      healthy_hosts.emplace_back(hosts[i]);
    }
    HostsPerLocalitySharedPtr healthy_hosts_per_locality =
        makeHostsPerLocality({healthy_hosts, {hosts[5]}});

    auto hosts = makeHostsFromHostsPerLocality(hosts_per_locality);
    host_set_->updateHosts(updateHostsParams(hosts, hosts_per_locality,
                                             makeHostsFromHostsPerLocality<HealthyHostVector>(
                                                 healthy_hosts_per_locality),
                                             healthy_hosts_per_locality),
                           locality_weights, {}, {}, absl::nullopt);
    locality_wrr_ = std::make_unique<LocalityWrr>(*host_set_, 0);
  };

  const auto expectPicks = [this](uint32_t locality_0_picks, uint32_t locality_1_picks) {
    uint32_t count[2] = {0, 0};
    for (uint32_t i = 0; i < 100; ++i) {
      const uint32_t locality_index = chooseHealthyLocality().value();
      ASSERT_LT(locality_index, 2);
      ++count[locality_index];
    }
    ENVOY_LOG_MISC(debug, "Locality picks {} {}", count[0], count[1]);
    EXPECT_EQ(locality_0_picks, count[0]);
    EXPECT_EQ(locality_1_picks, count[1]);
  };

  setHealthyHostCount(5);
  expectPicks(33, 67);
  setHealthyHostCount(4);
  expectPicks(33, 67);
  setHealthyHostCount(3);
  expectPicks(29, 71);
  setHealthyHostCount(2);
  expectPicks(22, 78);
  setHealthyHostCount(1);
  expectPicks(12, 88);
  setHealthyHostCount(0);
  expectPicks(0, 100);
}

TEST(OverProvisioningFactorTest, LocalityPickChanges) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.restart_features.move_locality_schedulers_to_lb", "false"}});
  auto setUpHostSetWithOPFAndTestPicks = [](const uint32_t overprovisioning_factor,
                                            const uint32_t pick_0, const uint32_t pick_1) {
    HostSetImpl host_set(0, false, overprovisioning_factor);
    std::shared_ptr<MockClusterInfo> cluster_info{new NiceMock<MockClusterInfo>()};
    auto time_source = std::make_unique<NiceMock<MockTimeSystem>>();
    envoy::config::core::v3::Locality zone_a;
    zone_a.set_zone("A");
    envoy::config::core::v3::Locality zone_b;
    zone_b.set_zone("B");
    HostVector hosts{makeTestHost(cluster_info, "tcp://127.0.0.1:80", zone_a),
                     makeTestHost(cluster_info, "tcp://127.0.0.1:81", zone_a),
                     makeTestHost(cluster_info, "tcp://127.0.0.1:82", zone_b)};
    LocalityWeightsConstSharedPtr locality_weights{new LocalityWeights{1, 1}};
    HostsPerLocalitySharedPtr hosts_per_locality =
        makeHostsPerLocality({{hosts[0], hosts[1]}, {hosts[2]}});
    // Healthy ratio: (1/2, 1).
    HostsPerLocalitySharedPtr healthy_hosts_per_locality =
        makeHostsPerLocality({{hosts[0]}, {hosts[2]}});
    auto healthy_hosts =
        makeHostsFromHostsPerLocality<HealthyHostVector>(healthy_hosts_per_locality);
    host_set.updateHosts(updateHostsParams(std::make_shared<const HostVector>(hosts),
                                           hosts_per_locality, healthy_hosts,
                                           healthy_hosts_per_locality),
                         locality_weights, {}, {}, absl::nullopt);
    LocalityWrr locality_wrr(host_set, 0);
    uint32_t cnts[] = {0, 0};
    for (uint32_t i = 0; i < 100; ++i) {
      absl::optional<uint32_t> locality_index = locality_wrr.chooseHealthyLocality();
      if (!locality_index.has_value()) {
        // It's possible locality scheduler is nullptr (when factor is 0).
        continue;
      }
      ASSERT_LT(locality_index.value(), 2);
      ++cnts[locality_index.value()];
    }
    EXPECT_EQ(pick_0, cnts[0]);
    EXPECT_EQ(pick_1, cnts[1]);
  };

  // NOTE: effective locality weight: weight * min(1, factor * healthy-ratio).

  // Picks in localities match to weight(1) * healthy-ratio when
  // overprovisioning factor is 1.
  setUpHostSetWithOPFAndTestPicks(100, 33, 67);
  // Picks in localities match to weights as factor * healthy-ratio > 1.
  setUpHostSetWithOPFAndTestPicks(200, 50, 50);
};

} // namespace
} // namespace Upstream
} // namespace Envoy
