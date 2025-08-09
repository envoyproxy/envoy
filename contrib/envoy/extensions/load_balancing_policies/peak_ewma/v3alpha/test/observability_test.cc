#include "source/common/network/address_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/common.h"
#include "test/mocks/upstream/host.h"

#include "contrib/envoy/extensions/load_balancing_policies/peak_ewma/v3alpha/source/observability.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace PeakEwma {

class ObservabilityTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create mock hosts with real addresses
    host1_ = std::make_shared<NiceMock<Upstream::MockHost>>();
    host2_ = std::make_shared<NiceMock<Upstream::MockHost>>();

    address1_ = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.1", 8080);
    address2_ = std::make_shared<Network::Address::Ipv4Instance>("10.0.0.2", 8080);

    ON_CALL(*host1_, address()).WillByDefault(Return(address1_));
    ON_CALL(*host2_, address()).WillByDefault(Return(address2_));

    observability_ =
        std::make_unique<Observability>(*store_.rootScope(), time_source_, cost_, default_rtt_ms_);
  }

  Stats::TestUtil::TestStore store_;
  NiceMock<MockTimeSystem> time_source_;
  Cost cost_;
  const double default_rtt_ms_ = 10.0;

  std::shared_ptr<NiceMock<Upstream::MockHost>> host1_;
  std::shared_ptr<NiceMock<Upstream::MockHost>> host2_;
  Network::Address::InstanceConstSharedPtr address1_;
  Network::Address::InstanceConstSharedPtr address2_;

  std::unique_ptr<Observability> observability_;
};

TEST_F(ObservabilityTest, CreateHostStats) {
  // Create stats for a host
  auto host_stats = observability_->createHostStats(host1_);

  ASSERT_NE(host_stats, nullptr);

  // Verify stats gauges were created with correct names
  auto& cost_gauge =
      store_.gauge("peak_ewma.10.0.0.1:8080.cost", Stats::Gauge::ImportMode::NeverImport);
  auto& ewma_gauge =
      store_.gauge("peak_ewma.10.0.0.1:8080.ewma_rtt_ms", Stats::Gauge::ImportMode::NeverImport);
  auto& req_gauge = store_.gauge("peak_ewma.10.0.0.1:8080.active_requests",
                                 Stats::Gauge::ImportMode::NeverImport);

  // Gauges should exist and be accessible
  EXPECT_EQ(cost_gauge.value(), 0);
  EXPECT_EQ(ewma_gauge.value(), 0);
  EXPECT_EQ(req_gauge.value(), 0);
}

TEST_F(ObservabilityTest, GlobalHostStatsSetters) {
  auto host_stats = observability_->createHostStats(host1_);

  // Test setting cost stat
  host_stats->setComputedCostStat(123.45);
  auto& cost_gauge =
      store_.gauge("peak_ewma.10.0.0.1:8080.cost", Stats::Gauge::ImportMode::NeverImport);
  EXPECT_EQ(cost_gauge.value(), 123); // Double converted to uint64_t

  // Test setting EWMA RTT stat
  host_stats->setEwmaRttStat(67.89);
  auto& ewma_gauge =
      store_.gauge("peak_ewma.10.0.0.1:8080.ewma_rtt_ms", Stats::Gauge::ImportMode::NeverImport);
  EXPECT_EQ(ewma_gauge.value(), 67);

  // Test setting active requests stat
  host_stats->setActiveRequestsStat(5.0);
  auto& req_gauge = store_.gauge("peak_ewma.10.0.0.1:8080.active_requests",
                                 Stats::Gauge::ImportMode::NeverImport);
  EXPECT_EQ(req_gauge.value(), 5);
}

TEST_F(ObservabilityTest, MultipleHosts) {
  // Create stats for multiple hosts
  auto host1_stats = observability_->createHostStats(host1_);
  auto host2_stats = observability_->createHostStats(host2_);

  // Set different values for each host
  host1_stats->setComputedCostStat(100.0);
  host1_stats->setEwmaRttStat(20.0);
  host1_stats->setActiveRequestsStat(3.0);

  host2_stats->setComputedCostStat(200.0);
  host2_stats->setEwmaRttStat(40.0);
  host2_stats->setActiveRequestsStat(7.0);

  // Verify each host has separate stats
  auto& host1_cost =
      store_.gauge("peak_ewma.10.0.0.1:8080.cost", Stats::Gauge::ImportMode::NeverImport);
  auto& host2_cost =
      store_.gauge("peak_ewma.10.0.0.2:8080.cost", Stats::Gauge::ImportMode::NeverImport);

  EXPECT_EQ(host1_cost.value(), 100);
  EXPECT_EQ(host2_cost.value(), 200);

  auto& host1_ewma =
      store_.gauge("peak_ewma.10.0.0.1:8080.ewma_rtt_ms", Stats::Gauge::ImportMode::NeverImport);
  auto& host2_ewma =
      store_.gauge("peak_ewma.10.0.0.2:8080.ewma_rtt_ms", Stats::Gauge::ImportMode::NeverImport);

  EXPECT_EQ(host1_ewma.value(), 20);
  EXPECT_EQ(host2_ewma.value(), 40);
}

TEST_F(ObservabilityTest, ReportMethod) {
  // Create host stats map like the load balancer would
  std::unordered_map<Upstream::HostConstSharedPtr, std::unique_ptr<GlobalHostStats>> all_host_stats;

  all_host_stats[host1_] = observability_->createHostStats(host1_);
  all_host_stats[host2_] = observability_->createHostStats(host2_);

  // Set some values
  all_host_stats[host1_]->setComputedCostStat(150.5);
  all_host_stats[host2_]->setEwmaRttStat(35.2);

  // Call report method (currently a no-op, but should not crash)
  EXPECT_NO_THROW(observability_->report(all_host_stats));

  // Verify stats were set correctly
  auto& host1_cost =
      store_.gauge("peak_ewma.10.0.0.1:8080.cost", Stats::Gauge::ImportMode::NeverImport);
  auto& host2_ewma =
      store_.gauge("peak_ewma.10.0.0.2:8080.ewma_rtt_ms", Stats::Gauge::ImportMode::NeverImport);

  EXPECT_EQ(host1_cost.value(), 150);
  EXPECT_EQ(host2_ewma.value(), 35);
}

TEST_F(ObservabilityTest, EdgeCaseValues) {
  auto host_stats = observability_->createHostStats(host1_);

  // Test with zero values
  host_stats->setComputedCostStat(0.0);
  host_stats->setEwmaRttStat(0.0);
  host_stats->setActiveRequestsStat(0.0);

  auto& cost_gauge =
      store_.gauge("peak_ewma.10.0.0.1:8080.cost", Stats::Gauge::ImportMode::NeverImport);
  auto& ewma_gauge =
      store_.gauge("peak_ewma.10.0.0.1:8080.ewma_rtt_ms", Stats::Gauge::ImportMode::NeverImport);
  auto& req_gauge = store_.gauge("peak_ewma.10.0.0.1:8080.active_requests",
                                 Stats::Gauge::ImportMode::NeverImport);

  EXPECT_EQ(cost_gauge.value(), 0);
  EXPECT_EQ(ewma_gauge.value(), 0);
  EXPECT_EQ(req_gauge.value(), 0);

  // Test with large values
  host_stats->setComputedCostStat(999999.99);
  host_stats->setEwmaRttStat(888888.88);
  host_stats->setActiveRequestsStat(777777.77);

  EXPECT_EQ(cost_gauge.value(), 999999);
  EXPECT_EQ(ewma_gauge.value(), 888888);
  EXPECT_EQ(req_gauge.value(), 777777);
}

TEST_F(ObservabilityTest, StatNamingWithDifferentAddresses) {
  // Test various address formats
  auto ipv6_address = std::make_shared<Network::Address::Ipv6Instance>("::1", 9090);
  auto host_ipv6 = std::make_shared<NiceMock<Upstream::MockHost>>();
  ON_CALL(*host_ipv6, address()).WillByDefault(Return(ipv6_address));

  auto host_ipv6_stats = observability_->createHostStats(host_ipv6);
  host_ipv6_stats->setComputedCostStat(42.0);

  // Verify IPv6 address is handled correctly in stat names
  // The exact format may vary, but should create a valid gauge
  auto gauge_names = store_.gauges();
  bool found_ipv6_stat = false;
  for (const auto& gauge : gauge_names) {
    if (gauge->name().find("::1") != std::string::npos &&
        gauge->name().find("cost") != std::string::npos) {
      found_ipv6_stat = true;
      EXPECT_EQ(gauge->value(), 42);
      break;
    }
  }
  EXPECT_TRUE(found_ipv6_stat);
}

TEST_F(ObservabilityTest, ConstructorParameters) {
  // Test constructor stores references correctly
  Cost different_cost;
  double different_default_rtt = 25.0;

  Observability custom_observability(*store_.rootScope(), time_source_, different_cost,
                                     different_default_rtt);

  // Create host stats with custom observability
  auto host_stats = custom_observability.createHostStats(host1_);

  // Should still work correctly
  host_stats->setComputedCostStat(99.9);

  auto& cost_gauge =
      store_.gauge("peak_ewma.10.0.0.1:8080.cost", Stats::Gauge::ImportMode::NeverImport);
  EXPECT_EQ(cost_gauge.value(), 99);
}

TEST_F(ObservabilityTest, StatsUpdateMultipleTimes) {
  auto host_stats = observability_->createHostStats(host1_);

  // Update stats multiple times
  host_stats->setComputedCostStat(10.0);
  host_stats->setEwmaRttStat(5.0);

  auto& cost_gauge =
      store_.gauge("peak_ewma.10.0.0.1:8080.cost", Stats::Gauge::ImportMode::NeverImport);
  auto& ewma_gauge =
      store_.gauge("peak_ewma.10.0.0.1:8080.ewma_rtt_ms", Stats::Gauge::ImportMode::NeverImport);

  EXPECT_EQ(cost_gauge.value(), 10);
  EXPECT_EQ(ewma_gauge.value(), 5);

  // Update again
  host_stats->setComputedCostStat(20.0);
  host_stats->setEwmaRttStat(15.0);

  EXPECT_EQ(cost_gauge.value(), 20);
  EXPECT_EQ(ewma_gauge.value(), 15);

  // Verify values changed
  EXPECT_NE(cost_gauge.value(), 10);
  EXPECT_NE(ewma_gauge.value(), 5);
}

} // namespace PeakEwma
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
