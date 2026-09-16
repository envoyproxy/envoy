#include "source/extensions/resource_monitors/cpu_utilization/cpu_paths.h"

#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ResourceMonitors {
namespace CpuUtilizationMonitor {
namespace {

using testing::Return;

// =============================================================================
// CpuPaths::V1 Tests
// =============================================================================

TEST(CpuPathsV1Test, GetSharesPathReturnsCorrectPath) {
  EXPECT_EQ(CpuPaths::V1::getSharesPath(), "/sys/fs/cgroup/cpu/cpu.shares");
}

TEST(CpuPathsV1Test, GetUsagePathReturnsCorrectPath) {
  EXPECT_EQ(CpuPaths::V1::getUsagePath(), "/sys/fs/cgroup/cpuacct/cpuacct.usage");
}

TEST(CpuPathsV1Test, GetCpuBasePathReturnsCorrectPath) {
  EXPECT_EQ(CpuPaths::V1::getCpuBasePath(), "/sys/fs/cgroup/cpu");
}

TEST(CpuPathsV1Test, GetCpuacctBasePathReturnsCorrectPath) {
  EXPECT_EQ(CpuPaths::V1::getCpuacctBasePath(), "/sys/fs/cgroup/cpuacct");
}

// =============================================================================
// CpuPaths::V2 Tests
// =============================================================================

TEST(CpuPathsV2Test, GetStatPathReturnsCorrectPath) {
  EXPECT_EQ(CpuPaths::V2::getStatPath(), "/sys/fs/cgroup/cpu.stat");
}

TEST(CpuPathsV2Test, GetMaxPathReturnsCorrectPath) {
  EXPECT_EQ(CpuPaths::V2::getMaxPath(), "/sys/fs/cgroup/cpu.max");
}

TEST(CpuPathsV2Test, GetEffectiveCpusPathReturnsCorrectPath) {
  EXPECT_EQ(CpuPaths::V2::getEffectiveCpusPath(), "/sys/fs/cgroup/cpuset.cpus.effective");
}

// =============================================================================
// CpuPaths::isV1() Detection Tests
// =============================================================================

TEST(CpuPathsDetectionTest, IsV1ReturnsTrueWhenAllFilesExist) {
  Filesystem::MockInstance mock_fs;

  // Mock both required V1 files as existing
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage")).WillOnce(Return(true));

  EXPECT_TRUE(CpuPaths::isV1(mock_fs));
}

TEST(CpuPathsDetectionTest, IsV1ReturnsFalseWhenSharesFileMissing) {
  Filesystem::MockInstance mock_fs;

  // Mock shares file as missing, usage file present
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares")).WillOnce(Return(false));
  // Short-circuit evaluation - usage file check may not be called
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(true));

  EXPECT_FALSE(CpuPaths::isV1(mock_fs));
}

TEST(CpuPathsDetectionTest, IsV1ReturnsFalseWhenUsageFileMissing) {
  Filesystem::MockInstance mock_fs;

  // Mock shares file present, usage file missing
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage")).WillOnce(Return(false));

  EXPECT_FALSE(CpuPaths::isV1(mock_fs));
}

// =============================================================================
// CpuPaths::isV2() Detection Tests
// =============================================================================

TEST(CpuPathsDetectionTest, IsV2ReturnsTrueWhenStatFileExists) {
  Filesystem::MockInstance mock_fs;

  // Keyed only on cpu.stat; the optional v2 files must not be probed.
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(true));

  EXPECT_TRUE(CpuPaths::isV2(mock_fs));
}

TEST(CpuPathsDetectionTest, IsV2ReturnsTrueWhenOnlyStatFileExists) {
  Filesystem::MockInstance mock_fs;

  // A normal v2 pod may expose only cpu.stat; detection must still succeed.
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max")).Times(0);
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective")).Times(0);

  EXPECT_TRUE(CpuPaths::isV2(mock_fs));
}

TEST(CpuPathsDetectionTest, IsV2ReturnsFalseWhenStatFileMissing) {
  Filesystem::MockInstance mock_fs;

  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(false));

  EXPECT_FALSE(CpuPaths::isV2(mock_fs));
}

} // namespace
} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
