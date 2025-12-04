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

// Test that paths are properly constructed (not empty or malformed)
TEST(CpuPathsV1Test, PathsAreWellFormed) {
  // Verify shares path starts with base and ends with file
  const auto shares_path = CpuPaths::V1::getSharesPath();
  EXPECT_TRUE(shares_path.find("/sys/fs/cgroup/cpu") == 0);
  EXPECT_TRUE(shares_path.find("cpu.shares") != std::string::npos);

  // Verify usage path starts with base and ends with file
  const auto usage_path = CpuPaths::V1::getUsagePath();
  EXPECT_TRUE(usage_path.find("/sys/fs/cgroup/cpuacct") == 0);
  EXPECT_TRUE(usage_path.find("cpuacct.usage") != std::string::npos);
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

// Test that paths are properly constructed (not empty or malformed)
TEST(CpuPathsV2Test, PathsAreWellFormed) {
  // Verify all V2 paths start with the correct base
  const auto stat_path = CpuPaths::V2::getStatPath();
  const auto max_path = CpuPaths::V2::getMaxPath();
  const auto effective_path = CpuPaths::V2::getEffectiveCpusPath();

  EXPECT_TRUE(stat_path.find("/sys/fs/cgroup") == 0);
  EXPECT_TRUE(max_path.find("/sys/fs/cgroup") == 0);
  EXPECT_TRUE(effective_path.find("/sys/fs/cgroup") == 0);

  // Verify file names are present
  EXPECT_TRUE(stat_path.find("cpu.stat") != std::string::npos);
  EXPECT_TRUE(max_path.find("cpu.max") != std::string::npos);
  EXPECT_TRUE(effective_path.find("cpuset.cpus.effective") != std::string::npos);
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

TEST(CpuPathsDetectionTest, IsV1ReturnsFalseWhenNoFilesExist) {
  Filesystem::MockInstance mock_fs;

  // Mock both V1 files as missing
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares")).WillOnce(Return(false));
  // Short-circuit evaluation - usage file check may not be called
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(CpuPaths::isV1(mock_fs));
}

// =============================================================================
// CpuPaths::isV2() Detection Tests
// =============================================================================

TEST(CpuPathsDetectionTest, IsV2ReturnsTrueWhenAllFilesExist) {
  Filesystem::MockInstance mock_fs;

  // Mock all three required V2 files as existing
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective")).WillOnce(Return(true));

  EXPECT_TRUE(CpuPaths::isV2(mock_fs));
}

TEST(CpuPathsDetectionTest, IsV2ReturnsFalseWhenStatFileMissing) {
  Filesystem::MockInstance mock_fs;

  // Mock stat file missing
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(false));
  // Short-circuit evaluation - other file checks may not be called
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(true));

  EXPECT_FALSE(CpuPaths::isV2(mock_fs));
}

TEST(CpuPathsDetectionTest, IsV2ReturnsFalseWhenMaxFileMissing) {
  Filesystem::MockInstance mock_fs;

  // Mock max file missing
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max")).WillOnce(Return(false));
  // Short-circuit evaluation - effective_cpus check may not be called
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(true));

  EXPECT_FALSE(CpuPaths::isV2(mock_fs));
}

TEST(CpuPathsDetectionTest, IsV2ReturnsFalseWhenEffectiveCpusFileMissing) {
  Filesystem::MockInstance mock_fs;

  // Mock effective cpus file missing
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective")).WillOnce(Return(false));

  EXPECT_FALSE(CpuPaths::isV2(mock_fs));
}

TEST(CpuPathsDetectionTest, IsV2ReturnsFalseWhenNoFilesExist) {
  Filesystem::MockInstance mock_fs;

  // Mock all V2 files as missing
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(false));
  // Short-circuit evaluation - other file checks may not be called
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(CpuPaths::isV2(mock_fs));
}

TEST(CpuPathsDetectionTest, IsV2ReturnsFalseWhenOnlyStatAndMaxExist) {
  Filesystem::MockInstance mock_fs;

  // Mock only 2 out of 3 files existing (edge case - partial v2)
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective")).WillOnce(Return(false));

  EXPECT_FALSE(CpuPaths::isV2(mock_fs));
}

// =============================================================================
// Cross-Version Detection Tests (V1 vs V2)
// =============================================================================

TEST(CpuPathsDetectionTest, CannotBeBothV1AndV2Simultaneously) {
  // This test ensures the detection logic is mutually exclusive in typical scenarios
  Filesystem::MockInstance mock_fs;

  // Setup: All V1 files exist, no V2 files exist
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage")).WillOnce(Return(true));

  EXPECT_TRUE(CpuPaths::isV1(mock_fs));

  // Reset mock for V2 check
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(CpuPaths::isV2(mock_fs));
}

TEST(CpuPathsDetectionTest, NeitherV1NorV2WhenNoFilesExist) {
  // Test system with no cgroup support at all
  Filesystem::MockInstance mock_fs;

  // Check V1
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares")).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(CpuPaths::isV1(mock_fs));

  // Check V2
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(CpuPaths::isV2(mock_fs));
}

// =============================================================================
// Path Consistency Tests
// =============================================================================

TEST(CpuPathsConsistencyTest, V1PathsDontOverlap) {
  // Ensure V1 paths are distinct
  const auto shares = CpuPaths::V1::getSharesPath();
  const auto usage = CpuPaths::V1::getUsagePath();
  const auto cpu_base = CpuPaths::V1::getCpuBasePath();
  const auto cpuacct_base = CpuPaths::V1::getCpuacctBasePath();

  EXPECT_NE(shares, usage);
  EXPECT_NE(cpu_base, cpuacct_base);

  // Base paths should be prefixes of their respective file paths
  EXPECT_TRUE(shares.find(cpu_base) == 0);
  EXPECT_TRUE(usage.find(cpuacct_base) == 0);
}

TEST(CpuPathsConsistencyTest, V2PathsDontOverlap) {
  // Ensure V2 paths are distinct
  const auto stat = CpuPaths::V2::getStatPath();
  const auto max = CpuPaths::V2::getMaxPath();
  const auto effective = CpuPaths::V2::getEffectiveCpusPath();

  EXPECT_NE(stat, max);
  EXPECT_NE(stat, effective);
  EXPECT_NE(max, effective);
}

TEST(CpuPathsConsistencyTest, V1AndV2PathsAreDifferent) {
  // Ensure V1 and V2 paths don't collide
  EXPECT_NE(CpuPaths::V1::getSharesPath(), CpuPaths::V2::getStatPath());
  EXPECT_NE(CpuPaths::V1::getSharesPath(), CpuPaths::V2::getMaxPath());
  EXPECT_NE(CpuPaths::V1::getUsagePath(), CpuPaths::V2::getStatPath());
  EXPECT_NE(CpuPaths::V1::getUsagePath(), CpuPaths::V2::getMaxPath());
}

// =============================================================================
// Idempotency Tests - Verify methods return consistent values
// =============================================================================

TEST(CpuPathsIdempotencyTest, V1PathsAreIdempotent) {
  // Call each method multiple times and verify results are identical
  const auto shares1 = CpuPaths::V1::getSharesPath();
  const auto shares2 = CpuPaths::V1::getSharesPath();
  const auto shares3 = CpuPaths::V1::getSharesPath();
  EXPECT_EQ(shares1, shares2);
  EXPECT_EQ(shares2, shares3);

  const auto usage1 = CpuPaths::V1::getUsagePath();
  const auto usage2 = CpuPaths::V1::getUsagePath();
  const auto usage3 = CpuPaths::V1::getUsagePath();
  EXPECT_EQ(usage1, usage2);
  EXPECT_EQ(usage2, usage3);

  const auto cpu_base1 = CpuPaths::V1::getCpuBasePath();
  const auto cpu_base2 = CpuPaths::V1::getCpuBasePath();
  EXPECT_EQ(cpu_base1, cpu_base2);

  const auto cpuacct_base1 = CpuPaths::V1::getCpuacctBasePath();
  const auto cpuacct_base2 = CpuPaths::V1::getCpuacctBasePath();
  EXPECT_EQ(cpuacct_base1, cpuacct_base2);
}

TEST(CpuPathsIdempotencyTest, V2PathsAreIdempotent) {
  // Call each method multiple times and verify results are identical
  const auto stat1 = CpuPaths::V2::getStatPath();
  const auto stat2 = CpuPaths::V2::getStatPath();
  const auto stat3 = CpuPaths::V2::getStatPath();
  EXPECT_EQ(stat1, stat2);
  EXPECT_EQ(stat2, stat3);

  const auto max1 = CpuPaths::V2::getMaxPath();
  const auto max2 = CpuPaths::V2::getMaxPath();
  const auto max3 = CpuPaths::V2::getMaxPath();
  EXPECT_EQ(max1, max2);
  EXPECT_EQ(max2, max3);

  const auto effective1 = CpuPaths::V2::getEffectiveCpusPath();
  const auto effective2 = CpuPaths::V2::getEffectiveCpusPath();
  const auto effective3 = CpuPaths::V2::getEffectiveCpusPath();
  EXPECT_EQ(effective1, effective2);
  EXPECT_EQ(effective2, effective3);
}

TEST(CpuPathsIdempotencyTest, DetectionIsConsistent) {
  // Verify detection methods return consistent results across multiple calls
  Filesystem::MockInstance mock_fs;

  // Test isV1 consistency
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares"))
      .Times(2)
      .WillRepeatedly(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage"))
      .Times(2)
      .WillRepeatedly(Return(true));

  const bool v1_result1 = CpuPaths::isV1(mock_fs);
  const bool v1_result2 = CpuPaths::isV1(mock_fs);
  EXPECT_EQ(v1_result1, v1_result2);
  EXPECT_TRUE(v1_result1);

  // Test isV2 consistency
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max")).Times(2).WillRepeatedly(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective"))
      .Times(2)
      .WillRepeatedly(Return(true));

  const bool v2_result1 = CpuPaths::isV2(mock_fs);
  const bool v2_result2 = CpuPaths::isV2(mock_fs);
  EXPECT_EQ(v2_result1, v2_result2);
  EXPECT_TRUE(v2_result1);
}

// =============================================================================
// Edge Case Tests - Additional partial file existence scenarios
// =============================================================================

TEST(CpuPathsEdgeCaseTest, IsV2WithOnlyStatFileExists) {
  Filesystem::MockInstance mock_fs;

  // Only stat file exists (incomplete v2 setup)
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max")).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(CpuPaths::isV2(mock_fs));
}

TEST(CpuPathsEdgeCaseTest, IsV2WithOnlyEffectiveCpusFileExists) {
  Filesystem::MockInstance mock_fs;

  // Only effective_cpus file exists
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(true));

  EXPECT_FALSE(CpuPaths::isV2(mock_fs));
}

TEST(CpuPathsEdgeCaseTest, IsV1WithOnlySharesFileExists) {
  Filesystem::MockInstance mock_fs;

  // Only shares file exists (incomplete v1 setup)
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage")).WillOnce(Return(false));

  EXPECT_FALSE(CpuPaths::isV1(mock_fs));
}

TEST(CpuPathsEdgeCaseTest, IsV1WithOnlyUsageFileExists) {
  Filesystem::MockInstance mock_fs;

  // Only usage file exists
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares")).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(true));

  EXPECT_FALSE(CpuPaths::isV1(mock_fs));
}

// =============================================================================
// Path Properties Tests - Verify specific properties of returned paths
// =============================================================================

TEST(CpuPathsPropertiesTest, AllPathsAreAbsolute) {
  // Verify all paths start with '/'
  EXPECT_EQ(CpuPaths::V1::getSharesPath()[0], '/');
  EXPECT_EQ(CpuPaths::V1::getUsagePath()[0], '/');
  EXPECT_EQ(CpuPaths::V1::getCpuBasePath()[0], '/');
  EXPECT_EQ(CpuPaths::V1::getCpuacctBasePath()[0], '/');

  EXPECT_EQ(CpuPaths::V2::getStatPath()[0], '/');
  EXPECT_EQ(CpuPaths::V2::getMaxPath()[0], '/');
  EXPECT_EQ(CpuPaths::V2::getEffectiveCpusPath()[0], '/');
}

TEST(CpuPathsPropertiesTest, AllPathsAreNonEmpty) {
  // Verify no path is empty
  EXPECT_FALSE(CpuPaths::V1::getSharesPath().empty());
  EXPECT_FALSE(CpuPaths::V1::getUsagePath().empty());
  EXPECT_FALSE(CpuPaths::V1::getCpuBasePath().empty());
  EXPECT_FALSE(CpuPaths::V1::getCpuacctBasePath().empty());

  EXPECT_FALSE(CpuPaths::V2::getStatPath().empty());
  EXPECT_FALSE(CpuPaths::V2::getMaxPath().empty());
  EXPECT_FALSE(CpuPaths::V2::getEffectiveCpusPath().empty());
}

TEST(CpuPathsPropertiesTest, PathsContainExpectedSubstrings) {
  // V1 paths should contain 'cgroup'
  EXPECT_NE(CpuPaths::V1::getSharesPath().find("cgroup"), std::string::npos);
  EXPECT_NE(CpuPaths::V1::getUsagePath().find("cgroup"), std::string::npos);

  // V2 paths should contain 'cgroup'
  EXPECT_NE(CpuPaths::V2::getStatPath().find("cgroup"), std::string::npos);
  EXPECT_NE(CpuPaths::V2::getMaxPath().find("cgroup"), std::string::npos);
  EXPECT_NE(CpuPaths::V2::getEffectiveCpusPath().find("cgroup"), std::string::npos);

  // Verify file extensions/names
  EXPECT_NE(CpuPaths::V1::getSharesPath().find(".shares"), std::string::npos);
  EXPECT_NE(CpuPaths::V1::getUsagePath().find(".usage"), std::string::npos);
  EXPECT_NE(CpuPaths::V2::getStatPath().find(".stat"), std::string::npos);
  EXPECT_NE(CpuPaths::V2::getMaxPath().find(".max"), std::string::npos);
  EXPECT_NE(CpuPaths::V2::getEffectiveCpusPath().find(".effective"), std::string::npos);
}

TEST(CpuPathsPropertiesTest, BasePathsAreShorterThanFilePaths) {
  // Base paths should be shorter than their corresponding file paths
  EXPECT_LT(CpuPaths::V1::getCpuBasePath().length(), CpuPaths::V1::getSharesPath().length());
  EXPECT_LT(CpuPaths::V1::getCpuacctBasePath().length(), CpuPaths::V1::getUsagePath().length());
}

TEST(CpuPathsPropertiesTest, PathsDontContainDoubleSlashes) {
  // Verify paths don't have malformed double slashes (except after protocol)
  auto has_double_slash = [](const std::string& path) {
    return path.find("//") != std::string::npos && path.find("//") != 0;
  };

  EXPECT_FALSE(has_double_slash(CpuPaths::V1::getSharesPath()));
  EXPECT_FALSE(has_double_slash(CpuPaths::V1::getUsagePath()));
  EXPECT_FALSE(has_double_slash(CpuPaths::V2::getStatPath()));
  EXPECT_FALSE(has_double_slash(CpuPaths::V2::getMaxPath()));
  EXPECT_FALSE(has_double_slash(CpuPaths::V2::getEffectiveCpusPath()));
}

// =============================================================================
// Realistic Scenario Tests
// =============================================================================

TEST(CpuPathsScenarioTest, SystemWithOnlyV1Support) {
  // Simulate a system running cgroup v1
  Filesystem::MockInstance mock_fs;

  // V1 files exist
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage")).WillOnce(Return(true));

  // V2 files don't exist
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));

  EXPECT_TRUE(CpuPaths::isV1(mock_fs));
  EXPECT_FALSE(CpuPaths::isV2(mock_fs));
}

TEST(CpuPathsScenarioTest, SystemWithOnlyV2Support) {
  // Simulate a system running cgroup v2 (modern Linux)
  Filesystem::MockInstance mock_fs;

  // V1 files don't exist
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares")).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));

  // V2 files exist
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max")).WillOnce(Return(true));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective")).WillOnce(Return(true));

  EXPECT_FALSE(CpuPaths::isV1(mock_fs));
  EXPECT_TRUE(CpuPaths::isV2(mock_fs));
}

TEST(CpuPathsScenarioTest, SystemWithNoCgroupSupport) {
  // Simulate a system without cgroup support (e.g., macOS, Windows, old Linux)
  Filesystem::MockInstance mock_fs;

  // No V1 files
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu/cpu.shares")).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuacct/cpuacct.usage"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));

  // No V2 files
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.stat")).WillOnce(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpu.max"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));
  EXPECT_CALL(mock_fs, fileExists("/sys/fs/cgroup/cpuset.cpus.effective"))
      .Times(testing::AtMost(1))
      .WillRepeatedly(Return(false));

  EXPECT_FALSE(CpuPaths::isV1(mock_fs));
  EXPECT_FALSE(CpuPaths::isV2(mock_fs));
}

} // namespace
} // namespace CpuUtilizationMonitor
} // namespace ResourceMonitors
} // namespace Extensions
} // namespace Envoy
