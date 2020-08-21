#include "common/network/utility.h"
#include "common/upstream/host_utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/upstream/cluster_info.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

TEST(HostUtilityTest, All) {
  auto cluster = std::make_shared<NiceMock<MockClusterInfo>>();
  HostSharedPtr host = makeTestHost(cluster, "tcp://127.0.0.1:80");
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
            "degraded_eds_health/pending_dynamic_removal/pending_active_hc",
            HostUtility::healthFlagsToString(*host));
}

} // namespace
} // namespace Upstream
} // namespace Envoy
