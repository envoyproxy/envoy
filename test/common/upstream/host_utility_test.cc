#include "source/common/network/utility.h"
#include "source/common/upstream/host_utility.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/test_common/stats_utility.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Contains;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::UnorderedElementsAreArray;

namespace Envoy {
namespace Upstream {
namespace {

static constexpr HostUtility::HostStatusSet UnknownStatus =
    1u << static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::UNKNOWN);
static constexpr HostUtility::HostStatusSet HealthyStatus =
    1u << static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::HEALTHY);
static constexpr HostUtility::HostStatusSet UnhealthyStatus =
    1u << static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::UNHEALTHY);
static constexpr HostUtility::HostStatusSet DrainingStatus =
    1u << static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::DRAINING);
static constexpr HostUtility::HostStatusSet TimeoutStatus =
    1u << static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::TIMEOUT);
static constexpr HostUtility::HostStatusSet DegradedStatus =
    1u << static_cast<uint32_t>(envoy::config::core::v3::HealthStatus::DEGRADED);

TEST(HostUtilityTest, All) {
  auto cluster = std::make_shared<NiceMock<MockClusterInfo>>();
  auto time_source = std::make_unique<NiceMock<MockTimeSystem>>();
  HostSharedPtr host = makeTestHost(cluster, "tcp://127.0.0.1:80", *time_source);
  EXPECT_EQ("healthy", HostUtility::healthFlagsToString(*host));

  host->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ("/failed_active_hc", HostUtility::healthFlagsToString(*host));

  host->healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  EXPECT_EQ("/failed_active_hc/failed_outlier_check", HostUtility::healthFlagsToString(*host));

  host->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ("/failed_outlier_check", HostUtility::healthFlagsToString(*host));

  host->healthFlagSet(Host::HealthFlag::FAILED_EDS_HEALTH);
  EXPECT_EQ("/failed_outlier_check/failed_eds_health", HostUtility::healthFlagsToString(*host));

  host->healthFlagClear(Host::HealthFlag::FAILED_EDS_HEALTH);
  EXPECT_EQ("/failed_outlier_check", HostUtility::healthFlagsToString(*host));

  // Invokes healthFlagSet for each health flag.
#define SET_HEALTH_FLAG(name, notused) host->healthFlagSet(Host::HealthFlag::name);
  HEALTH_FLAG_ENUM_VALUES(SET_HEALTH_FLAG)
#undef SET_HEALTH_FLAG
  EXPECT_EQ("/failed_active_hc/failed_outlier_check/failed_eds_health/degraded_active_hc/"
            "degraded_eds_health/pending_dynamic_removal/pending_active_hc/"
            "excluded_via_immediate_hc_fail/active_hc_timeout/eds_status_draining",
            HostUtility::healthFlagsToString(*host));
}

TEST(HostLogging, FmtUtils) {
  auto cluster = std::make_shared<NiceMock<MockClusterInfo>>();
  auto time_source = std::make_unique<NiceMock<MockTimeSystem>>();
  auto time_ms = std::chrono::milliseconds(5);
  ON_CALL(*time_source, monotonicTime()).WillByDefault(Return(MonotonicTime(time_ms)));
  EXPECT_LOG_CONTAINS("warn", "Logging host info 127.0.0.1:80 end", {
    HostSharedPtr host = makeTestHost(cluster, "tcp://127.0.0.1:80", *time_source);
    ENVOY_LOG_MISC(warn, "Logging host info {} end", *host);
  });
  EXPECT_LOG_CONTAINS("warn", "Logging host info hostname end", {
    HostSharedPtr host = makeTestHost(cluster, "hostname", "tcp://127.0.0.1:80", *time_source);
    ENVOY_LOG_MISC(warn, "Logging host info {} end", *host);
  });
}

TEST(HostUtilityTest, CreateOverrideHostStatus) {

  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNKNOWN);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::HEALTHY);
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), UnknownStatus | HealthyStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNHEALTHY);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DRAINING);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::TIMEOUT);

    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config),
              UnhealthyStatus | DrainingStatus | TimeoutStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DEGRADED);
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), DegradedStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config),
              UnknownStatus | HealthyStatus | DegradedStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNHEALTHY);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DRAINING);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::TIMEOUT);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNKNOWN);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::HEALTHY);

    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config),
              UnhealthyStatus | DrainingStatus | TimeoutStatus | UnknownStatus | HealthyStatus);
  }

  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNHEALTHY);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DRAINING);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::TIMEOUT);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNKNOWN);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::HEALTHY);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DEGRADED);
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config),
              UnknownStatus | HealthyStatus | UnhealthyStatus | DrainingStatus | TimeoutStatus |
                  DegradedStatus);
  }
}

TEST(HostUtilityTest, SelectOverrideHostTest) {

  NiceMock<Upstream::MockLoadBalancerContext> context;

  const HostUtility::HostStatusSet all_health_statuses = UnknownStatus | HealthyStatus |
                                                         UnhealthyStatus | DrainingStatus |
                                                         TimeoutStatus | DegradedStatus;

  {
    // No valid host map.
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(nullptr, all_health_statuses, &context));
  }
  {
    // No valid load balancer context.
    auto host_map = std::make_shared<HostMap>();
    EXPECT_EQ(nullptr,
              HostUtility::selectOverrideHost(host_map.get(), all_health_statuses, nullptr));
  }
  {
    // No valid expected host.
    EXPECT_CALL(context, overrideHostToSelect()).WillOnce(Return(absl::nullopt));
    auto host_map = std::make_shared<HostMap>();
    EXPECT_EQ(nullptr,
              HostUtility::selectOverrideHost(host_map.get(), all_health_statuses, &context));
  }

  // Test overriding host in strict and non-strict mode.
  for (const bool strict_mode : {false, true}) {
    {
      // The host map does not contain the expected host.
      LoadBalancerContext::OverrideHost override_host{"1.2.3.4", strict_mode};
      EXPECT_CALL(context, overrideHostToSelect())
          .WillOnce(Return(absl::make_optional(override_host)));
      auto host_map = std::make_shared<HostMap>();
      EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), HealthyStatus, &context));
    }
    {
      auto mock_host = std::make_shared<NiceMock<MockHost>>();
      EXPECT_CALL(*mock_host, healthStatus())
          .WillRepeatedly(Return(envoy::config::core::v3::HealthStatus::UNHEALTHY));

      LoadBalancerContext::OverrideHost override_host{"1.2.3.4", strict_mode};
      EXPECT_CALL(context, overrideHostToSelect())
          .WillRepeatedly(Return(absl::make_optional(override_host)));

      auto host_map = std::make_shared<HostMap>();
      host_map->insert({"1.2.3.4", mock_host});

      EXPECT_EQ(mock_host,
                HostUtility::selectOverrideHost(host_map.get(), UnhealthyStatus, &context));
      EXPECT_EQ(mock_host,
                HostUtility::selectOverrideHost(host_map.get(), all_health_statuses, &context));

      EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), HealthyStatus, &context));
      EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), DegradedStatus, &context));
      EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), TimeoutStatus, &context));
      EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), DrainingStatus, &context));
      EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), UnknownStatus, &context));
    }
    {
      auto mock_host = std::make_shared<NiceMock<MockHost>>();
      EXPECT_CALL(*mock_host, healthStatus())
          .WillRepeatedly(Return(envoy::config::core::v3::HealthStatus::DEGRADED));

      LoadBalancerContext::OverrideHost override_host{"1.2.3.4", strict_mode};
      EXPECT_CALL(context, overrideHostToSelect())
          .WillRepeatedly(Return(absl::make_optional(override_host)));

      auto host_map = std::make_shared<HostMap>();
      host_map->insert({"1.2.3.4", mock_host});
      EXPECT_EQ(mock_host,
                HostUtility::selectOverrideHost(host_map.get(), DegradedStatus, &context));
      EXPECT_EQ(mock_host,
                HostUtility::selectOverrideHost(host_map.get(), all_health_statuses, &context));

      EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), HealthyStatus, &context));
      EXPECT_EQ(nullptr,
                HostUtility::selectOverrideHost(host_map.get(), UnhealthyStatus, &context));
      EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), TimeoutStatus, &context));
      EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), DrainingStatus, &context));
      EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), UnknownStatus, &context));
    }
  }
}

class PerEndpointMetricsTest : public testing::Test, public PerEndpointMetricsTestHelper {
public:
  std::pair<std::vector<Stats::PrimitiveCounterSnapshot>,
            std::vector<Stats::PrimitiveGaugeSnapshot>>
  run() {
    std::vector<Stats::PrimitiveCounterSnapshot> counters;
    std::vector<Stats::PrimitiveGaugeSnapshot> gauges;
    HostUtility::forEachHostMetric(
        cm_,
        [&](Stats::PrimitiveCounterSnapshot&& metric) { counters.emplace_back(std::move(metric)); },
        [&](Stats::PrimitiveGaugeSnapshot&& metric) { gauges.emplace_back(std::move(metric)); });

    return std::make_pair(counters, gauges);
  }
};

template <class MetricType>
std::vector<std::string> metricNames(const std::vector<MetricType>& metrics) {
  std::vector<std::string> names;
  names.reserve(metrics.size());
  for (const auto& metric : metrics) {
    names.push_back(metric.name());
  }
  return names;
}

template <class MetricType>
std::vector<std::pair<std::string, uint64_t>>
metricNamesAndValues(const std::vector<MetricType>& metrics) {
  std::vector<std::pair<std::string, uint64_t>> ret;
  ret.reserve(metrics.size());
  for (const auto& metric : metrics) {
    ret.push_back(std::make_pair(metric.name(), metric.value()));
  }
  return ret;
}

template <class MetricType>
const MetricType& getMetric(absl::string_view name, const std::vector<MetricType>& metrics) {
  for (const auto& metric : metrics) {
    if (metric.name() == name) {
      return metric;
    }
  }
  PANIC("not found");
}

TEST_F(PerEndpointMetricsTest, Basic) {
  makeCluster("mycluster", 1);
  auto [counters, gauges] = run();

  EXPECT_THAT(metricNamesAndValues(counters),
              UnorderedElementsAreArray({
                  std::make_pair("cluster.mycluster.endpoint.127.0.0.1_80.c1", 11),
                  std::make_pair("cluster.mycluster.endpoint.127.0.0.1_80.c2", 12),
              }));
  EXPECT_THAT(metricNamesAndValues(gauges),
              UnorderedElementsAreArray({
                  std::make_pair("cluster.mycluster.endpoint.127.0.0.1_80.g1", 13),
                  std::make_pair("cluster.mycluster.endpoint.127.0.0.1_80.g2", 14),
                  std::make_pair("cluster.mycluster.endpoint.127.0.0.1_80.healthy", 1),
              }));
}

// Warming clusters are not included
TEST_F(PerEndpointMetricsTest, Warming) {
  makeCluster("mycluster", 1);
  makeCluster("warming", 1, true /* warming */);

  auto [counters, gauges] = run();

  EXPECT_THAT(metricNames(counters),
              UnorderedElementsAreArray({"cluster.mycluster.endpoint.127.0.0.1_80.c1",
                                         "cluster.mycluster.endpoint.127.0.0.1_80.c2"}));
  EXPECT_THAT(metricNames(gauges),
              UnorderedElementsAreArray({"cluster.mycluster.endpoint.127.0.0.1_80.g1",
                                         "cluster.mycluster.endpoint.127.0.0.1_80.g2",
                                         "cluster.mycluster.endpoint.127.0.0.1_80.healthy"}));
}

TEST_F(PerEndpointMetricsTest, HealthyGaugeUnhealthy) {
  auto& cluster = makeCluster("mycluster", 0);
  auto& host = addHost(cluster);
  EXPECT_CALL(host, coarseHealth()).WillOnce(Return(Host::Health::Unhealthy));
  auto [counters, gauges] = run();
  EXPECT_EQ(getMetric("cluster.mycluster.endpoint.127.0.0.1_80.healthy", gauges).value(), 0);
}

TEST_F(PerEndpointMetricsTest, HealthyGaugeDegraded) {
  auto& cluster = makeCluster("mycluster", 0);
  auto& host = addHost(cluster);
  EXPECT_CALL(host, coarseHealth()).WillOnce(Return(Host::Health::Degraded));
  auto [counters, gauges] = run();
  EXPECT_EQ(getMetric("cluster.mycluster.endpoint.127.0.0.1_80.healthy", gauges).value(), 0);
}

TEST_F(PerEndpointMetricsTest, MultipleClustersAndHosts) {
  makeCluster("cluster1", 2);
  makeCluster("cluster2", 3);

  auto [counters, gauges] = run();

  EXPECT_THAT(metricNamesAndValues(counters),
              UnorderedElementsAreArray({
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.c1", 11),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.c2", 12),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.c1", 21),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.c2", 22),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.3_80.c1", 31),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.3_80.c2", 32),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.4_80.c1", 41),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.4_80.c2", 42),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.5_80.c1", 51),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.5_80.c2", 52),
              }));
  EXPECT_THAT(metricNamesAndValues(gauges),
              UnorderedElementsAreArray({
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.g1", 13),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.g2", 14),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.healthy", 1),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.g1", 23),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.g2", 24),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.healthy", 1),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.3_80.g1", 33),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.3_80.g2", 34),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.3_80.healthy", 1),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.4_80.g1", 43),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.4_80.g2", 44),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.4_80.healthy", 1),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.5_80.g1", 53),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.5_80.g2", 54),
                  std::make_pair("cluster.cluster2.endpoint.127.0.0.5_80.healthy", 1),
              }));
}

TEST_F(PerEndpointMetricsTest, MultiplePriorityLevels) {
  auto& cluster = makeCluster("cluster1", 1);
  addHost(cluster, 2 /* non-default priority level */);

  auto [counters, gauges] = run();

  EXPECT_THAT(metricNamesAndValues(counters),
              UnorderedElementsAreArray({
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.c1", 11),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.c2", 12),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.c1", 21),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.c2", 22),
              }));
  EXPECT_THAT(metricNamesAndValues(gauges),
              UnorderedElementsAreArray({
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.g1", 13),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.g2", 14),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.1_80.healthy", 1),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.g1", 23),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.g2", 24),
                  std::make_pair("cluster.cluster1.endpoint.127.0.0.2_80.healthy", 1),
              }));
}

TEST_F(PerEndpointMetricsTest, Tags) {
  auto& cluster = makeCluster("cluster1", 0);
  auto& host1 = addHost(cluster);
  std::string hostname = "host.example.com";
  EXPECT_CALL(host1, hostname()).WillOnce(ReturnRef(hostname));
  addHost(cluster);

  auto [counters, gauges] = run();

  // Only the first host has a hostname, so only it has that tag.
  EXPECT_THAT(getMetric("cluster.cluster1.endpoint.127.0.0.1_80.c1", counters).tags(),
              UnorderedElementsAreArray({
                  Stats::Tag{"envoy.cluster_name", "cluster1"},
                  Stats::Tag{"envoy.endpoint_address", "127.0.0.1:80"},
                  Stats::Tag{"envoy.endpoint_hostname", hostname},
              }));

  EXPECT_THAT(getMetric("cluster.cluster1.endpoint.127.0.0.2_80.c1", counters).tags(),
              UnorderedElementsAreArray({
                  Stats::Tag{"envoy.cluster_name", "cluster1"},
                  Stats::Tag{"envoy.endpoint_address", "127.0.0.2:80"},
              }));
}

TEST_F(PerEndpointMetricsTest, FixedTags) {
  auto& cluster = makeCluster("cluster1", 1);
  Stats::TagVector fixed_tags{{"fixed1", "value1"}, {"fixed2", "value2"}};
  EXPECT_CALL(cluster.info_->stats_store_, fixedTags()).WillOnce(ReturnRef(fixed_tags));

  auto [counters, gauges] = run();

  EXPECT_THAT(getMetric("cluster.cluster1.endpoint.127.0.0.1_80.c1", counters).tags(),
              UnorderedElementsAreArray({
                  Stats::Tag{"envoy.cluster_name", "cluster1"},
                  Stats::Tag{"envoy.endpoint_address", "127.0.0.1:80"},
                  Stats::Tag{"fixed1", "value1"},
                  Stats::Tag{"fixed2", "value2"},
              }));
}

// Only clusters with the setting enabled produce metrics.
TEST_F(PerEndpointMetricsTest, Enabled) {
  auto& disabled = makeCluster("disabled", 1);
  auto& enabled = makeCluster("enabled", 1);
  EXPECT_CALL(*disabled.info_, perEndpointStatsEnabled()).WillOnce(Return(false));
  EXPECT_CALL(*enabled.info_, perEndpointStatsEnabled()).WillOnce(Return(true));

  auto [counters, gauges] = run();

  EXPECT_THAT(metricNames(counters),
              UnorderedElementsAreArray({"cluster.enabled.endpoint.127.0.0.2_80.c1",
                                         "cluster.enabled.endpoint.127.0.0.2_80.c2"}));
  EXPECT_THAT(metricNames(gauges),
              UnorderedElementsAreArray({"cluster.enabled.endpoint.127.0.0.2_80.g1",
                                         "cluster.enabled.endpoint.127.0.0.2_80.g2",
                                         "cluster.enabled.endpoint.127.0.0.2_80.healthy"}));
}

// Stats use observability name, and are sanitized.
TEST_F(PerEndpointMetricsTest, SanitizedObservabilityName) {
  auto& cluster = makeCluster("notthisname", 1);
  std::string name = "observability:name";
  EXPECT_CALL(*cluster.info_, observabilityName()).WillOnce(ReturnRef(name));

  auto [counters, gauges] = run();

  EXPECT_THAT(metricNames(counters),
              UnorderedElementsAreArray({"cluster.observability_name.endpoint.127.0.0.1_80.c1",
                                         "cluster.observability_name.endpoint.127.0.0.1_80.c2"}));
  EXPECT_THAT(
      metricNames(gauges),
      UnorderedElementsAreArray({"cluster.observability_name.endpoint.127.0.0.1_80.g1",
                                 "cluster.observability_name.endpoint.127.0.0.1_80.g2",
                                 "cluster.observability_name.endpoint.127.0.0.1_80.healthy"}));

  EXPECT_THAT(getMetric("cluster.observability_name.endpoint.127.0.0.1_80.c1", counters).tags(),
              Contains(Stats::Tag{"envoy.cluster_name", "observability_name"}));
}

} // namespace
} // namespace Upstream
} // namespace Envoy
