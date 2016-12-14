#include "common/upstream/host_utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/mocks/upstream/mocks.h"

namespace Upstream {

TEST(HostUtilityTest, All) {
  MockCluster cluster;
  HostImpl host(cluster, "tcp://127.0.0.1:80", false, 1, "");
  EXPECT_EQ("healthy", HostUtility::healthFlagsToString(host));

  host.healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ("/failed_active_hc", HostUtility::healthFlagsToString(host));

  host.healthFlagSet(Host::HealthFlag::FAILED_OUTLIER_CHECK);
  EXPECT_EQ("/failed_active_hc/failed_outlier_check", HostUtility::healthFlagsToString(host));

  host.healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  EXPECT_EQ("/failed_outlier_check", HostUtility::healthFlagsToString(host));
}

} // Upstream
