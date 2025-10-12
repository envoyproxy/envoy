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
// These run only in EngFlow RBE environments with linux_x64_small pool (2 CPUs).
// Tests basic cgroup detection without heavy server integration dependencies.
class CgroupCpuSimpleIntegrationTest : public testing::Test {
protected:
  void SetUp() override {
    // Skip test if not in CI environment
    if (!TestEnvironment::getOptionalEnvVar("CI").has_value()) {
      GTEST_SKIP() << "Skipping cgroup test - not in CI environment";
    }
  }
};

// Test basic cgroup CPU detection functionality
TEST_F(CgroupCpuSimpleIntegrationTest, CgroupDetectionBasicFunctionality) {
  CgroupDetectorImpl detector;
  Filesystem::InstanceImpl fs;

  auto cpu_limit = detector.getCpuLimit(fs);

  if (cpu_limit.has_value()) {
    // If cgroup limit is detected, it should be exactly 2 for linux_x64_small pool
    uint32_t limit = cpu_limit.value();
    EXPECT_EQ(2U, limit); // linux_x64_small pool has exactly 2 CPUs

    ENVOY_LOG_MISC(info, "Cgroup CPU limit detected: {}", limit);
  } else {
    // No cgroup limit detected - this is also valid (unlimited/no cgroup)
    ENVOY_LOG_MISC(info, "No cgroup CPU limit detected (unlimited)");
  }

  // Test should not crash regardless of cgroup configuration
  SUCCEED();
}

// Test environment variable disable functionality
TEST_F(CgroupCpuSimpleIntegrationTest, EnvironmentVariableDisable) {
  // Set environment variable to disable cgroup detection
  TestEnvironment::setEnvVar("ENVOY_CGROUP_CPU_DETECTION", "false", 1);

  CgroupDetectorImpl detector;
  Filesystem::InstanceImpl fs;

  // When disabled via env var, should return no limit
  auto cpu_limit = detector.getCpuLimit(fs);

  // With detection disabled, should return no limit
  EXPECT_FALSE(cpu_limit.has_value());

  ENVOY_LOG_MISC(info, "Cgroup detection disabled via env var - no limit returned");

  // Clean up
  TestEnvironment::unsetEnvVar("ENVOY_CGROUP_CPU_DETECTION");
}

// Test singleton pattern works correctly
TEST_F(CgroupCpuSimpleIntegrationTest, SingletonPattern) {
  auto& detector1 = CgroupDetectorSingleton::get();
  auto& detector2 = CgroupDetectorSingleton::get();

  // Should be the same instance
  EXPECT_EQ(&detector1, &detector2);

  Filesystem::InstanceImpl fs;
  auto cpu_limit = detector1.getCpuLimit(fs);

  ENVOY_LOG_MISC(info, "Singleton pattern verified - CPU limit: {}",
                 cpu_limit.has_value() ? std::to_string(cpu_limit.value()) : "unlimited");

  SUCCEED();
}

#endif // __linux__

} // namespace
} // namespace Server
} // namespace Envoy
