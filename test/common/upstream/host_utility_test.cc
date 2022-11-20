#include "source/common/network/utility.h"
#include "source/common/upstream/host_utility.h"
#include "source/common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host.h"
#include "test/mocks/upstream/load_balancer_context.h"

#include "gmock/gmock.h"

using ::testing::Return;

namespace Envoy {
namespace Upstream {
namespace {

static constexpr HostUtility::HostStatusSet UnhealthyStatus =
    1u << static_cast<size_t>(Host::Health::Unhealthy);
static constexpr HostUtility::HostStatusSet DegradedStatus =
    1u << static_cast<size_t>(Host::Health::Degraded);
static constexpr HostUtility::HostStatusSet HealthyStatus =
    1u << static_cast<size_t>(Host::Health::Healthy);

TEST(HostUtilityTest, All) {
  auto cluster = std::make_shared<NiceMock<MockClusterInfo>>();
  auto time_source = std::make_unique<NiceMock<MockTimeSystem>>();
  auto time_ms = std::chrono::milliseconds(5);
  ON_CALL(*time_source, monotonicTime()).WillByDefault(Return(MonotonicTime(time_ms)));
  HostSharedPtr host = makeTestHost(cluster, "tcp://127.0.0.1:80", *time_source);
  EXPECT_EQ("healthy", HostUtility::healthFlagsToString(*host));
  EXPECT_EQ(time_ms, std::chrono::time_point_cast<std::chrono::milliseconds>(host->creationTime())
                         .time_since_epoch());

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
            "excluded_via_immediate_hc_fail/active_hc_timeout",
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
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), HealthyStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::UNHEALTHY);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DRAINING);
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::TIMEOUT);

    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), UnhealthyStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    lb_config.mutable_override_host_status()->add_statuses(
        ::envoy::config::core::v3::HealthStatus::DEGRADED);
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), DegradedStatus);
  }
  {
    envoy::config::cluster::v3::Cluster::CommonLbConfig lb_config;
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), 0b110u);
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

    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), 0b101u);
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
    EXPECT_EQ(HostUtility::createOverrideHostStatus(lb_config), 0b111u);
  }
}

TEST(HostUtilityTest, SelectOverrideHostTest) {

  NiceMock<Upstream::MockLoadBalancerContext> context;

  const HostUtility::HostStatusSet all_health_statuses =
      UnhealthyStatus | DegradedStatus | HealthyStatus;

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
  {
    // The host map does not contain the expected host.
    LoadBalancerContext::OverrideHost override_host{"1.2.3.4"};
    EXPECT_CALL(context, overrideHostToSelect())
        .WillOnce(Return(absl::make_optional(override_host)));
    auto host_map = std::make_shared<HostMap>();
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), HealthyStatus, &context));
  }
  {
    // The status of host is not as expected.
    auto mock_host = std::make_shared<NiceMock<MockHost>>();
    EXPECT_CALL(*mock_host, coarseHealth()).WillOnce(Return(Host::Health::Unhealthy));

    LoadBalancerContext::OverrideHost override_host{"1.2.3.4"};
    EXPECT_CALL(context, overrideHostToSelect())
        .WillOnce(Return(absl::make_optional(override_host)));

    auto host_map = std::make_shared<HostMap>();
    host_map->insert({"1.2.3.4", mock_host});
    EXPECT_EQ(nullptr, HostUtility::selectOverrideHost(host_map.get(), HealthyStatus, &context));
  }
  {
    // Get expected host.
    auto mock_host = std::make_shared<NiceMock<MockHost>>();
    EXPECT_CALL(*mock_host, coarseHealth()).WillOnce(Return(Host::Health::Degraded));

    LoadBalancerContext::OverrideHost override_host{"1.2.3.4"};
    EXPECT_CALL(context, overrideHostToSelect())
        .WillOnce(Return(absl::make_optional(override_host)));

    auto host_map = std::make_shared<HostMap>();
    host_map->insert({"1.2.3.4", mock_host});
    EXPECT_EQ(mock_host, HostUtility::selectOverrideHost(host_map.get(),
                                                         HealthyStatus | DegradedStatus, &context));
  }
}

} // namespace
} // namespace Upstream
} // namespace Envoy
