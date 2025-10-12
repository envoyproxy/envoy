#ifdef __linux__
#include "source/server/cgroup_cpu_util.h"

#endif

#include "test/server/utility.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

#ifdef __linux__

// CI-only tests for cgroup CPU detection server concurrency integration.
// These run only in EngFlow RBE environments with linux_x64_small pool (2 CPUs).
// Tests follow the exact pattern from ServerInstanceImplTest::Stats for concurrency verification.
class CgroupServerIntegrationTest : public ServerInstanceImplTestBase,
                                    public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  CgroupServerIntegrationTest() { version_ = GetParam(); }

  void SetUp() override {
    // Skip test if not in CI environment
    if (!TestEnvironment::getOptionalEnvVar("CI").has_value()) {
      GTEST_SKIP() << "Skipping cgroup test - not in CI environment";
    }
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CgroupServerIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test cgroup CPU detection with server concurrency - follows ServerInstanceImplTest::Stats pattern
TEST_P(CgroupServerIntegrationTest, CgroupCpuDetectionWithServerConcurrency) {
  // Following the exact pattern from ServerInstanceImplTest::Stats
  options_.service_cluster_name_ = "test_cluster";
  options_.service_node_name_ = "test_node";
  // Don't set options_.concurrency_ - let it auto-detect from cgroups

  EXPECT_NO_THROW(initialize("test/server/test_data/server/empty_bootstrap.yaml"));

  // Following the exact pattern from ServerInstanceImplTest::Stats
  auto concurrency_gauge = TestUtility::findGauge(stats_store_, "server.concurrency");
  EXPECT_NE(nullptr, concurrency_gauge);

  // In EngFlow RBE linux_x64_small pool (2 CPUs), verify cgroup detection works
  uint64_t concurrency = concurrency_gauge->value();
  EXPECT_EQ(2L, concurrency); // linux_x64_small pool has exactly 2 CPUs
}

// Test that explicit concurrency overrides cgroup detection - matches Stats test pattern
TEST_P(CgroupServerIntegrationTest, ExplicitConcurrencyOverridesCgroup) {
  // Following ServerInstanceImplTest::Stats pattern exactly
  options_.service_cluster_name_ = "test_cluster";
  options_.service_node_name_ = "test_node";
  options_.concurrency_ = 1; // Explicit override like Stats test

  EXPECT_NO_THROW(initialize("test/server/test_data/server/empty_bootstrap.yaml"));

  // Exact same pattern as ServerInstanceImplTest::Stats
  EXPECT_EQ(1L, TestUtility::findGauge(stats_store_, "server.concurrency")->value());
}

#endif // __linux__

} // namespace
} // namespace Server
} // namespace Envoy
