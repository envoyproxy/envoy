#ifdef __linux__
#include "source/server/cgroup_cpu_util.h"

#endif

#include "source/common/filesystem/filesystem_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

#ifdef __linux__

// This test runs in CI with Docker CPU limits (--cpus=2) to verify 'cgroups' detection.
// Tagged 'runtime-cpu' to run only in controlled environments with CPU limits.
// Tests basic cgroup detection without heavy server integration dependencies.
class CgroupCpuSimpleIntegrationTest : public testing::Test {};

// Test basic cgroup CPU detection functionality
// In CI environment with Docker CPU limits (--cpus=2), we expect to detect 'cgroups' limits
TEST_F(CgroupCpuSimpleIntegrationTest, CgroupDetectionBasicFunctionality) {
  CgroupDetectorImpl detector;
  Filesystem::InstanceImpl fs;

  auto cpu_limit = detector.getCpuLimit(fs);

  // In CI environment with Docker CPU limits, we MUST detect 'cgroups'
  ASSERT_TRUE(cpu_limit.has_value())
      << "Cgroups not detected in CI environment - Docker CPU limits not working";

  // Should be exactly 2 for our Docker --cpus=2 configuration
  uint32_t limit = cpu_limit.value();
  EXPECT_EQ(2U, limit) << "Expected 2 CPUs from Docker --cpus=2 setting, got: " << limit;

  ENVOY_LOG_MISC(info, "Cgroup CPU limit detected: {}", limit);
}

#endif // __linux__

} // namespace
} // namespace Server
} // namespace Envoy
