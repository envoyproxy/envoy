#include "common/network/utility.h"
#include "common/upstream/host_utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/common.h"
#include "test/mocks/upstream/cluster_info.h"

#include "gmock/gmock.h"

using ::testing::Return;

namespace Envoy {
namespace Upstream {
namespace {

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

} // namespace
} // namespace Upstream
} // namespace Envoy
