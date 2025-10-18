#ifdef __linux__
#include "source/server/cgroup_cpu_util.h"

#endif

#include "source/common/filesystem/filesystem_impl.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Server {
namespace {

#ifdef __linux__

// CI-only simple tests for cgroup CPU detection functionality.
// These run only in EngFlow 'RBE' environments with linux_x64_small pool (2 CPUs).
// Tests basic cgroup detection without heavy server integration dependencies.
class CgroupCpuSimpleIntegrationTest : public testing::Test {
};

// Test basic cgroup CPU detection functionality - MUST have 'cgroups' in CI
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
