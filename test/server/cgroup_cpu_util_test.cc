// Unit tests for cgroup CPU utility functions
// Following the test patterns from Go's cgroup implementation
// See: https://github.com/golang/go/blob/master/src/internal/cgroup/cgroup_linux_test.go

#include "source/common/filesystem/filesystem_impl.h"
#include "source/server/cgroup_cpu_util.h"

#include "test/mocks/filesystem/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {

// Test helper to create a mock filesystem that returns specific file contents
class MockFilesystemForCgroup : public NiceMock<Filesystem::MockInstance> {
public:
  void setFileContents(const std::string& path, const std::string& contents) {
    file_contents_[path] = contents;
  }

  absl::StatusOr<std::string> fileReadToEnd(const std::string& path) override {
    auto it = file_contents_.find(path);
    if (it != file_contents_.end()) {
      return it->second;
    }
    return absl::InvalidArgumentError("File not found");
  }

private:
  std::map<std::string, std::string> file_contents_;
};

// Helper function to escape paths like Linux's show_path
// Converts '\', ' ', '\t', and '\n' to octal escape sequences
std::string escapePath(const std::string& path) {
  std::string result;
  for (char c : path) {
    switch (c) {
    case '\\':
      result += "\\134";
      break;
    case ' ':
      result += "\\040";
      break;
    case '\t':
      result += "\\011";
      break;
    case '\n':
      result += "\\012";
      break;
    default:
      result += c;
      break;
    }
  }
  return result;
}

// Test class that can access private members via friend declaration
class CgroupCpuUtilTest : public testing::Test {
protected:
  MockFilesystemForCgroup fs_;
};

// =============================================================================
// Test: getCurrentCgroupPath
// =============================================================================

TEST_F(CgroupCpuUtilTest, GetCurrentCgroupPath_Empty) {
  // Using fs_ member variable
  fs_.setFileContents("/proc/self/cgroup", "");

  auto result = CgroupCpuUtil::TestUtil::getCurrentCgroupPath(fs_);
  EXPECT_FALSE(result.has_value()); // Empty file
}

TEST_F(CgroupCpuUtilTest, GetCurrentCgroupPath_V1) {
  // Using fs_ member variable
  fs_.setFileContents("/proc/self/cgroup", "2:cpu,cpuacct:/a/b/cpu\n"
                                           "1:blkio:/a/b/blkio\n");

  auto result = CgroupCpuUtil::TestUtil::getCurrentCgroupPath(fs_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().relative_path, "/a/b/cpu");
  EXPECT_EQ(result.value().version, "v1");
}

TEST_F(CgroupCpuUtilTest, GetCurrentCgroupPath_V2) {
  // Using fs_ member variable
  fs_.setFileContents("/proc/self/cgroup", "0::/a/b/c\n");

  auto result = CgroupCpuUtil::TestUtil::getCurrentCgroupPath(fs_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().relative_path, "/a/b/c");
  EXPECT_EQ(result.value().version, "v2");
}

TEST_F(CgroupCpuUtilTest, GetCurrentCgroupPath_Mixed_V1Wins) {
  // Using fs_ member variable
  fs_.setFileContents("/proc/self/cgroup", "2:cpu,cpuacct:/a/b/cpu\n"
                                           "1:blkio:/a/b/blkio\n"
                                           "0::/a/b/v2\n");

  auto result = CgroupCpuUtil::TestUtil::getCurrentCgroupPath(fs_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().relative_path, "/a/b/cpu"); // v1 takes precedence
  EXPECT_EQ(result.value().version, "v1");
}

TEST_F(CgroupCpuUtilTest, GetCurrentCgroupPath_MalformedLine) {
  // Using fs_ member variable
  fs_.setFileContents("/proc/self/cgroup", "malformed\n"
                                           "0::/a/b/c\n");

  auto result = CgroupCpuUtil::TestUtil::getCurrentCgroupPath(fs_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().relative_path, "/a/b/c"); // Skips malformed, finds v2
  EXPECT_EQ(result.value().version, "v2");
}

TEST_F(CgroupCpuUtilTest, GetCurrentCgroupPath_V2EmptyPath) {
  // Test consistent behavior: even if v2 path is empty, we should return it if v2 hierarchy
  // exists (return empty string, not nullopt)
  fs_.setFileContents("/proc/self/cgroup", "0::\n"); // Empty path after hierarchy "0"

  auto result = CgroupCpuUtil::TestUtil::getCurrentCgroupPath(fs_);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value().relative_path, ""); // Should return empty string, not nullopt
  EXPECT_EQ(result.value().version, "v2");
}

TEST_F(CgroupCpuUtilTest, GetCurrentCgroupPath_OnlyNonCpuV1) {
  // Test case where we have v1 hierarchies but none with CPU controller
  // Should return nullopt since no v2 and no v1 CPU controller
  fs_.setFileContents("/proc/self/cgroup", "1:memory:/a/b/memory\n"
                                           "2:blkio:/a/b/blkio\n");

  auto result = CgroupCpuUtil::TestUtil::getCurrentCgroupPath(fs_);
  EXPECT_FALSE(result.has_value()); // No v2 and no v1 CPU = nullopt
}

// =============================================================================
// Test: unescapePath
// =============================================================================

TEST_F(CgroupCpuUtilTest, UnescapePath_Boring) {
  std::string input = "/a/b/c";
  std::string result = CgroupCpuUtil::TestUtil::unescapePath(input);
  EXPECT_EQ(result, "/a/b/c");
}

TEST_F(CgroupCpuUtilTest, UnescapePath_Space) {
  std::string input = "/a/b\\040b/c";
  std::string result = CgroupCpuUtil::TestUtil::unescapePath(input);
  EXPECT_EQ(result, "/a/b b/c");
}

TEST_F(CgroupCpuUtilTest, UnescapePath_Tab) {
  std::string input = "/a/b\\011b/c";
  std::string result = CgroupCpuUtil::TestUtil::unescapePath(input);
  EXPECT_EQ(result, "/a/b\tb/c");
}

TEST_F(CgroupCpuUtilTest, UnescapePath_Newline) {
  std::string input = "/a/b\\012b/c";
  std::string result = CgroupCpuUtil::TestUtil::unescapePath(input);
  EXPECT_EQ(result, "/a/b\nb/c");
}

TEST_F(CgroupCpuUtilTest, UnescapePath_Backslash) {
  std::string input = "/a/b\\134b/c";
  std::string result = CgroupCpuUtil::TestUtil::unescapePath(input);
  EXPECT_EQ(result, "/a/b\\b/c");
}

TEST_F(CgroupCpuUtilTest, UnescapePath_BackslashAtBeginning) {
  std::string input = "\\134b/c";
  std::string result = CgroupCpuUtil::TestUtil::unescapePath(input);
  EXPECT_EQ(result, "\\b/c");
}

TEST_F(CgroupCpuUtilTest, UnescapePath_BackslashAtEnd) {
  std::string input = "/a/\\134";
  std::string result = CgroupCpuUtil::TestUtil::unescapePath(input);
  EXPECT_EQ(result, "/a/\\");
}

TEST_F(CgroupCpuUtilTest, UnescapePath_MultipleEscapes) {
  std::string input = "/a\\040b\\011c\\012d\\134e";
  std::string result = CgroupCpuUtil::TestUtil::unescapePath(input);
  EXPECT_EQ(result, "/a b\tc\nd\\e");
}

TEST_F(CgroupCpuUtilTest, UnescapePath_InvalidEscapeNotEnoughChars) {
  std::string input = "/a/b\\04";
  std::string result = CgroupCpuUtil::TestUtil::unescapePath(input);
  // Should keep backslash as-is when invalid
  EXPECT_EQ(result, "/a/b\\04");
}

TEST_F(CgroupCpuUtilTest, UnescapePath_InvalidEscapeNonOctal) {
  std::string input = "/a/b\\xyz";
  std::string result = CgroupCpuUtil::TestUtil::unescapePath(input);
  // Should keep backslash as-is when invalid
  EXPECT_EQ(result, "/a/b\\xyz");
}

// =============================================================================
// Test: parseMountInfoLine
// =============================================================================

TEST_F(CgroupCpuUtilTest, ParseMountInfoLine_V1) {
  std::string line = "56 22 0:40 / /sys/fs/cgroup/cpu rw - cgroup cgroup rw,cpu,cpuacct";

  auto mount_opt = CgroupCpuUtil::TestUtil::parseMountInfoLine(line);
  ASSERT_TRUE(mount_opt.has_value());
  const auto& mount_point = mount_opt.value();
  EXPECT_EQ(mount_point, "/sys/fs/cgroup/cpu");
}

TEST_F(CgroupCpuUtilTest, ParseMountInfoLine_V2) {
  std::string line = "25 21 0:22 / /sys/fs/cgroup rw,nosuid,nodev,noexec - cgroup2 cgroup2 rw";

  auto mount_opt = CgroupCpuUtil::TestUtil::parseMountInfoLine(line);
  ASSERT_TRUE(mount_opt.has_value());
  const auto& mount_point = mount_opt.value();
  EXPECT_EQ(mount_point, "/sys/fs/cgroup");
}

TEST_F(CgroupCpuUtilTest, ParseMountInfoLine_V1NoCPU) {
  std::string line = "49 22 0:37 / /sys/fs/cgroup/memory rw - cgroup cgroup rw,memory";

  auto mount_opt = CgroupCpuUtil::TestUtil::parseMountInfoLine(line);
  ASSERT_TRUE(mount_opt.has_value());
  const auto& mount_point = mount_opt.value();
  EXPECT_EQ(mount_point, "/sys/fs/cgroup/memory");
}

TEST_F(CgroupCpuUtilTest, ParseMountInfoLine_Escaped) {
  std::string line =
      "25 21 0:22 / /sys/fs/cgroup/tab\\011tab rw,nosuid,nodev,noexec - cgroup2 cgroup2 rw";

  auto mount_opt = CgroupCpuUtil::TestUtil::parseMountInfoLine(line);
  ASSERT_TRUE(mount_opt.has_value());
  const auto& mount_point = mount_opt.value();
  EXPECT_EQ(mount_point, "/sys/fs/cgroup/tab\ttab"); // Unescaped
}

TEST_F(CgroupCpuUtilTest, ParseMountInfoLine_NotCgroup) {
  std::string line = "22 1 8:1 / / rw,relatime - ext4 /dev/root rw";

  auto mount_opt = CgroupCpuUtil::TestUtil::parseMountInfoLine(line);
  EXPECT_FALSE(mount_opt.has_value()); // Not a cgroup filesystem
}

TEST_F(CgroupCpuUtilTest, ParseMountInfoLine_MalformedNoSeparator) {
  std::string line = "25 21 0:22 / /sys/fs/cgroup rw,nosuid,nodev,noexec cgroup2 cgroup2 rw";

  EXPECT_LOG_CONTAINS("warn", "Malformed mountinfo line: separator", {
    auto mount_opt = CgroupCpuUtil::TestUtil::parseMountInfoLine(line);
    EXPECT_FALSE(mount_opt.has_value()); // Malformed - no separator
  });
}

TEST_F(CgroupCpuUtilTest, ParseMountInfoLine_MalformedInsufficientFields) {
  std::string line = "25 21 - cgroup"; // Has separator but only 4 fields (needs at least 5)

  EXPECT_LOG_CONTAINS("warn", "expected at least 5 fields", {
    auto mount_opt = CgroupCpuUtil::TestUtil::parseMountInfoLine(line);
    EXPECT_FALSE(mount_opt.has_value()); // Malformed - not enough fields
  });
}

// =============================================================================
// Test: discoverCgroupMount
// =============================================================================

TEST_F(CgroupCpuUtilTest, DiscoverCgroupMount_Empty) {
  // Using fs_ member variable
  fs_.setFileContents("/proc/self/mountinfo", "");

  auto mount_opt = CgroupCpuUtil::TestUtil::discoverCgroupMount(fs_);
  EXPECT_FALSE(mount_opt.has_value()); // No cgroup mounts
}

TEST_F(CgroupCpuUtilTest, DiscoverCgroupMount_V1) {
  // Using fs_ member variable
  std::string mountinfo = "22 1 8:1 / / rw,relatime - ext4 /dev/root rw\n"
                          "20 22 0:19 / /proc rw,nosuid,nodev,noexec - proc proc rw\n"
                          "21 22 0:20 / /sys rw,nosuid,nodev,noexec - sysfs sysfs rw\n"
                          "49 22 0:37 / /sys/fs/cgroup/memory rw - cgroup cgroup rw,memory\n"
                          "54 22 0:38 / /sys/fs/cgroup/io rw - cgroup cgroup rw,io\n"
                          "56 22 0:40 / /sys/fs/cgroup/cpu rw - cgroup cgroup rw,cpu,cpuacct\n"
                          "58 22 0:42 / /sys/fs/cgroup/net rw - cgroup cgroup rw,net\n"
                          "59 22 0:43 / /sys/fs/cgroup/cpuset rw - cgroup cgroup rw,cpuset\n";
  fs_.setFileContents("/proc/self/mountinfo", mountinfo);

  auto mount_opt = CgroupCpuUtil::TestUtil::discoverCgroupMount(fs_);
  ASSERT_TRUE(mount_opt.has_value());
  EXPECT_EQ(mount_opt.value(), "/sys/fs/cgroup/cpu"); // Should return v1 CPU mount point
}

TEST_F(CgroupCpuUtilTest, DiscoverCgroupMount_V2) {
  // Using fs_ member variable
  std::string mountinfo =
      "22 1 8:1 / / rw,relatime - ext4 /dev/root rw\n"
      "20 22 0:19 / /proc rw,nosuid,nodev,noexec - proc proc rw\n"
      "21 22 0:20 / /sys rw,nosuid,nodev,noexec - sysfs sysfs rw\n"
      "25 21 0:22 / /sys/fs/cgroup rw,nosuid,nodev,noexec - cgroup2 cgroup2 rw\n";
  fs_.setFileContents("/proc/self/mountinfo", mountinfo);

  auto mount_opt = CgroupCpuUtil::TestUtil::discoverCgroupMount(fs_);
  ASSERT_TRUE(mount_opt.has_value());
  EXPECT_EQ(mount_opt.value(), "/sys/fs/cgroup"); // Should return v2 mount point
}

TEST_F(CgroupCpuUtilTest, DiscoverCgroupMount_Mixed_V1Wins) {
  // Using fs_ member variable
  std::string mountinfo =
      "22 1 8:1 / / rw,relatime - ext4 /dev/root rw\n"
      "20 22 0:19 / /proc rw,nosuid,nodev,noexec - proc proc rw\n"
      "21 22 0:20 / /sys rw,nosuid,nodev,noexec - sysfs sysfs rw\n"
      "25 21 0:22 / /sys/fs/cgroup rw,nosuid,nodev,noexec - cgroup2 cgroup2 rw\n"
      "49 22 0:37 / /sys/fs/cgroup/memory rw - cgroup cgroup rw,memory\n"
      "54 22 0:38 / /sys/fs/cgroup/io rw - cgroup cgroup rw,io\n"
      "56 22 0:40 / /sys/fs/cgroup/cpu rw - cgroup cgroup rw,cpu,cpuacct\n"
      "58 22 0:42 / /sys/fs/cgroup/net rw - cgroup cgroup rw,net\n"
      "59 22 0:43 / /sys/fs/cgroup/cpuset rw - cgroup cgroup rw,cpuset\n";
  fs_.setFileContents("/proc/self/mountinfo", mountinfo);

  auto mount_opt = CgroupCpuUtil::TestUtil::discoverCgroupMount(fs_);
  ASSERT_TRUE(mount_opt.has_value());
  EXPECT_EQ(mount_opt.value(), "/sys/fs/cgroup/cpu"); // v1 CPU takes precedence over v2
}

TEST_F(CgroupCpuUtilTest, DiscoverCgroupMount_V2Escaped) {
  // Using fs_ member variable
  std::string mountinfo =
      "22 1 8:1 / / rw,relatime - ext4 /dev/root rw\n"
      "20 22 0:19 / /proc rw,nosuid,nodev,noexec - proc proc rw\n"
      "21 22 0:20 / /sys rw,nosuid,nodev,noexec - sysfs sysfs rw\n"
      "25 21 0:22 / /sys/fs/cgroup/tab\\011tab rw,nosuid,nodev,noexec - cgroup2 cgroup2 rw\n";
  fs_.setFileContents("/proc/self/mountinfo", mountinfo);

  auto mount_opt = CgroupCpuUtil::TestUtil::discoverCgroupMount(fs_);
  ASSERT_TRUE(mount_opt.has_value());
  EXPECT_EQ(mount_opt.value(), "/sys/fs/cgroup/tab\ttab"); // Should be unescaped
}

// =============================================================================
// Test: Helper - escapePath
// =============================================================================

TEST_F(CgroupCpuUtilTest, EscapePath_Boring) { EXPECT_EQ(escapePath("/a/b/c"), "/a/b/c"); }

TEST_F(CgroupCpuUtilTest, EscapePath_Space) { EXPECT_EQ(escapePath("/a/b b/c"), "/a/b\\040b/c"); }

TEST_F(CgroupCpuUtilTest, EscapePath_Tab) { EXPECT_EQ(escapePath("/a/b\tb/c"), "/a/b\\011b/c"); }

TEST_F(CgroupCpuUtilTest, EscapePath_Newline) {
  EXPECT_EQ(escapePath("/a/b\nb/c"), "/a/b\\012b/c");
}

TEST_F(CgroupCpuUtilTest, EscapePath_Backslash) {
  EXPECT_EQ(escapePath("/a/b\\b/c"), "/a/b\\134b/c");
}

TEST_F(CgroupCpuUtilTest, EscapePath_Beginning) { EXPECT_EQ(escapePath("\\b/c"), "\\134b/c"); }

TEST_F(CgroupCpuUtilTest, EscapePath_Ending) { EXPECT_EQ(escapePath("/a/\\"), "/a/\\134"); }

// =============================================================================
// Test: Round-trip escape/unescape
// =============================================================================

TEST_F(CgroupCpuUtilTest, EscapeUnescape_Roundtrip) {
  std::vector<std::string> test_cases = {
      "/a/b/c",    "/a/b b/c",
      "/a/b\tb/c", "/a/b\nb/c",
      "/a/b\\b/c", "\\b/c",
      "/a/\\",     "/my path/with spaces/and\ttabs/and\nnewlines\\backslash"};

  for (const auto& original : test_cases) {
    std::string escaped = escapePath(original);
    std::string unescaped = CgroupCpuUtil::TestUtil::unescapePath(escaped);
    EXPECT_EQ(unescaped, original) << "Failed for: " << original;
  }
}

// Test validateCgroupFileContent function - valid content
TEST_F(CgroupCpuUtilTest, ValidateCgroupFileContent_Valid) {
  const std::string content = "500000 100000\n";
  const std::string file_path = "/test/file";

  auto result = CgroupCpuUtil::TestUtil::validateCgroupFileContent(content, file_path);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value(), "500000 100000");
}

// Test validateCgroupFileContent function - missing newline
TEST_F(CgroupCpuUtilTest, ValidateCgroupFileContent_MissingNewline) {
  const std::string content = "500000 100000";
  const std::string file_path = "/test/file";

  auto result = CgroupCpuUtil::TestUtil::validateCgroupFileContent(content, file_path);
  EXPECT_FALSE(result.has_value());
}

// Test validateCgroupFileContent function - empty content
TEST_F(CgroupCpuUtilTest, ValidateCgroupFileContent_Empty) {
  const std::string content = "";
  const std::string file_path = "/test/file";

  auto result = CgroupCpuUtil::TestUtil::validateCgroupFileContent(content, file_path);
  EXPECT_FALSE(result.has_value());
}

// =============================================================================
// Tests for our new `modularized` functions
// =============================================================================

// =============================================================================
// Test: accessCgroupV1Files (`modularized` file access for v1)
// =============================================================================

TEST_F(CgroupCpuUtilTest, AccessCgroupV1Files_Success) {
  // Test successful v1 file access and caching
  CgroupInfo info{"/sys/fs/cgroup/cpu/docker/container123", "v1"};

  fs_.setFileContents("/sys/fs/cgroup/cpu/docker/container123/cpu.cfs_quota_us", "150000\n");
  fs_.setFileContents("/sys/fs/cgroup/cpu/docker/container123/cpu.cfs_period_us", "100000\n");

  auto result = CgroupCpuUtil::TestUtil::accessCgroupV1Files(info, fs_);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->version, "v1");
  EXPECT_EQ(result->quota_content, "150000\n");
  EXPECT_EQ(result->period_content, "100000\n");
}

TEST_F(CgroupCpuUtilTest, AccessCgroupV1Files_QuotaFileMissing) {
  // Test when quota file is missing
  CgroupInfo info{"/sys/fs/cgroup/cpu/docker/container123", "v1"};

  // Only set period file, quota file missing
  fs_.setFileContents("/sys/fs/cgroup/cpu/docker/container123/cpu.cfs_period_us", "100000\n");

  EXPECT_LOG_CONTAINS("warn", "Expected cgroup v1 files not accessible", {
    auto result = CgroupCpuUtil::TestUtil::accessCgroupV1Files(info, fs_);
    EXPECT_FALSE(result.has_value());
  });
}

TEST_F(CgroupCpuUtilTest, AccessCgroupV1Files_PeriodFileMissing) {
  // Test when period file is missing
  CgroupInfo info{"/sys/fs/cgroup/cpu/docker/container123", "v1"};

  // Only set quota file, period file missing
  fs_.setFileContents("/sys/fs/cgroup/cpu/docker/container123/cpu.cfs_quota_us", "150000\n");

  EXPECT_LOG_CONTAINS("warn", "Expected cgroup v1 files not accessible", {
    auto result = CgroupCpuUtil::TestUtil::accessCgroupV1Files(info, fs_);
    EXPECT_FALSE(result.has_value());
  });
}

TEST_F(CgroupCpuUtilTest, AccessCgroupV1Files_BothFilesMissing) {
  // Test when both files are missing
  CgroupInfo info{"/sys/fs/cgroup/cpu/docker/container123", "v1"};

  // No files set in filesystem mock

  EXPECT_LOG_CONTAINS("warn", "Expected cgroup v1 files not accessible", {
    auto result = CgroupCpuUtil::TestUtil::accessCgroupV1Files(info, fs_);
    EXPECT_FALSE(result.has_value());
  });
}

// =============================================================================
// Test: accessCgroupV2Files (`modularized` file access for v2)
// =============================================================================

TEST_F(CgroupCpuUtilTest, AccessCgroupV2Files_Success) {
  // Test successful v2 file access and caching
  CgroupInfo info{"/sys/fs/cgroup/user.slice/user-1000.slice", "v2"};

  fs_.setFileContents("/sys/fs/cgroup/user.slice/user-1000.slice/cpu.max", "250000 100000\n");

  auto result = CgroupCpuUtil::TestUtil::accessCgroupV2Files(info, fs_);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->version, "v2");
  EXPECT_EQ(result->quota_content, "250000 100000\n");
  EXPECT_EQ(result->period_content, ""); // v2 doesn't use separate period file
}

TEST_F(CgroupCpuUtilTest, AccessCgroupV2Files_FileMissing) {
  // Test when cpu.max file is missing
  CgroupInfo info{"/sys/fs/cgroup/user.slice/user-1000.slice", "v2"};

  // No cpu.max file set in filesystem mock

  EXPECT_LOG_CONTAINS("warn", "Expected cgroup v2 file not accessible", {
    auto result = CgroupCpuUtil::TestUtil::accessCgroupV2Files(info, fs_);
    EXPECT_FALSE(result.has_value());
  });
}

TEST_F(CgroupCpuUtilTest, AccessCgroupV2Files_UnlimitedContent) {
  // Test v2 file with unlimited content
  CgroupInfo info{"/sys/fs/cgroup/system.slice", "v2"};

  fs_.setFileContents("/sys/fs/cgroup/system.slice/cpu.max", "max 100000\n");

  auto result = CgroupCpuUtil::TestUtil::accessCgroupV2Files(info, fs_);

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->version, "v2");
  EXPECT_EQ(result->quota_content, "max 100000\n");
  EXPECT_EQ(result->period_content, "");
}

// =============================================================================
// Test: readActualLimitsV1 (`modularized` parsing for v1)
// =============================================================================

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV1_Success) {
  // Test successful v1 limit parsing with cached content
  CpuFiles cpu_files;
  cpu_files.version = "v1";
  cpu_files.quota_content = "150000\n";
  cpu_files.period_content = "100000\n";

  auto result = CgroupCpuUtil::TestUtil::readActualLimitsV1(cpu_files);

  ASSERT_TRUE(result.has_value());       // Valid result
  EXPECT_DOUBLE_EQ(result.value(), 1.5); // 150000/100000 = 1.5
}

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV1_Unlimited) {
  // Test v1 unlimited scenario (quota = -1)
  CpuFiles cpu_files;
  cpu_files.version = "v1";
  cpu_files.quota_content = "-1\n";
  cpu_files.period_content = "100000\n";

  auto result = CgroupCpuUtil::TestUtil::readActualLimitsV1(cpu_files);

  EXPECT_FALSE(result.has_value()); // Unlimited returns nullopt
}

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV1_QuotaParseError) {
  // Test parsing error in quota
  CpuFiles cpu_files;
  cpu_files.version = "v1";
  cpu_files.quota_content = "150000us\n"; // Invalid: contains units
  cpu_files.period_content = "100000\n";

  EXPECT_LOG_CONTAINS("warn", "Failed to parse cgroup v1 values", {
    auto result = CgroupCpuUtil::TestUtil::readActualLimitsV1(cpu_files);
    EXPECT_FALSE(result.has_value()); // Parse error returns nullopt
  });
}

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV1_PeriodParseError) {
  // Test parsing error in period
  CpuFiles cpu_files;
  cpu_files.version = "v1";
  cpu_files.quota_content = "150000\n";
  cpu_files.period_content = "100000us\n"; // Invalid: contains units

  EXPECT_LOG_CONTAINS("warn", "Failed to parse cgroup v1 values", {
    auto result = CgroupCpuUtil::TestUtil::readActualLimitsV1(cpu_files);
    EXPECT_FALSE(result.has_value()); // Parse error returns nullopt
  });
}

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV1_ZeroQuota) {
  // Test invalid zero quota
  CpuFiles cpu_files;
  cpu_files.version = "v1";
  cpu_files.quota_content = "0\n";
  cpu_files.period_content = "100000\n";

  EXPECT_LOG_CONTAINS("warn", "Invalid cgroup v1 values: quota=0", {
    auto result = CgroupCpuUtil::TestUtil::readActualLimitsV1(cpu_files);
    EXPECT_FALSE(result.has_value()); // Invalid values return nullopt
  });
}

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV1_ZeroPeriod) {
  // Test invalid zero period (division by zero protection)
  CpuFiles cpu_files;
  cpu_files.version = "v1";
  cpu_files.quota_content = "150000\n";
  cpu_files.period_content = "0\n";

  EXPECT_LOG_CONTAINS("warn", "Invalid cgroup v1 values: quota=150000 period=0", {
    auto result = CgroupCpuUtil::TestUtil::readActualLimitsV1(cpu_files);
    EXPECT_FALSE(result.has_value()); // Invalid values return nullopt
  });
}

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV1_WhitespaceHandling) {
  // Test whitespace handling in cached content
  CpuFiles cpu_files;
  cpu_files.version = "v1";
  cpu_files.quota_content = "  150000  \n";  // Leading/trailing whitespace
  cpu_files.period_content = "\t100000\t\n"; // Tabs and spaces

  auto result = CgroupCpuUtil::TestUtil::readActualLimitsV1(cpu_files);

  ASSERT_TRUE(result.has_value()); // Valid result
  EXPECT_DOUBLE_EQ(result.value(), 1.5);
}

// =============================================================================
// Test: readActualLimitsV2 (`modularized` parsing for v2)
// =============================================================================

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV2_Success) {
  // Test successful v2 limit parsing with cached content
  CpuFiles cpu_files;
  cpu_files.version = "v2";
  cpu_files.quota_content = "250000 100000\n";
  cpu_files.period_content = ""; // v2 doesn't use separate period file

  auto result = CgroupCpuUtil::TestUtil::readActualLimitsV2(cpu_files);

  ASSERT_TRUE(result.has_value());       // Valid result
  EXPECT_DOUBLE_EQ(result.value(), 2.5); // 250000/100000 = 2.5
}

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV2_Unlimited) {
  // Test v2 unlimited scenario (quota = "max")
  CpuFiles cpu_files;
  cpu_files.version = "v2";
  cpu_files.quota_content = "max 100000\n";
  cpu_files.period_content = "";

  auto result = CgroupCpuUtil::TestUtil::readActualLimitsV2(cpu_files);

  EXPECT_FALSE(result.has_value()); // Unlimited returns nullopt
}

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV2_MalformedFormat) {
  // Test malformed v2 format (missing space separator)
  CpuFiles cpu_files;
  cpu_files.version = "v2";
  cpu_files.quota_content = "250000\n"; // v1-style format
  cpu_files.period_content = "";

  EXPECT_LOG_CONTAINS("warn", "Malformed cgroup v2 cpu.max", {
    auto result = CgroupCpuUtil::TestUtil::readActualLimitsV2(cpu_files);
    EXPECT_FALSE(result.has_value()); // Malformed returns nullopt
  });
}

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV2_QuotaParseError) {
  // Test parsing error in quota part
  CpuFiles cpu_files;
  cpu_files.version = "v2";
  cpu_files.quota_content = "250000us 100000\n"; // Invalid: contains units
  cpu_files.period_content = "";

  EXPECT_LOG_CONTAINS("warn", "Failed to parse cgroup v2 values", {
    auto result = CgroupCpuUtil::TestUtil::readActualLimitsV2(cpu_files);
    EXPECT_FALSE(result.has_value()); // Parse error returns nullopt
  });
}

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV2_PeriodParseError) {
  // Test parsing error in period part
  CpuFiles cpu_files;
  cpu_files.version = "v2";
  cpu_files.quota_content = "250000 100000us\n"; // Invalid: contains units
  cpu_files.period_content = "";

  EXPECT_LOG_CONTAINS("warn", "Failed to parse cgroup v2 values", {
    auto result = CgroupCpuUtil::TestUtil::readActualLimitsV2(cpu_files);
    EXPECT_FALSE(result.has_value()); // Parse error returns nullopt
  });
}

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV2_ZeroPeriod) {
  // Test invalid zero period (division by zero protection)
  CpuFiles cpu_files;
  cpu_files.version = "v2";
  cpu_files.quota_content = "250000 0\n";
  cpu_files.period_content = "";

  EXPECT_LOG_CONTAINS("warn", "Invalid cgroup v2 period: cannot be zero", {
    auto result = CgroupCpuUtil::TestUtil::readActualLimitsV2(cpu_files);
    EXPECT_FALSE(result.has_value()); // Invalid values return nullopt
  });
}

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV2_WhitespaceHandling) {
  // Test whitespace handling in cached content
  CpuFiles cpu_files;
  cpu_files.version = "v2";
  cpu_files.quota_content = "  250000 100000  \n"; // Leading/trailing whitespace
  cpu_files.period_content = "";

  auto result = CgroupCpuUtil::TestUtil::readActualLimitsV2(cpu_files);

  ASSERT_TRUE(result.has_value()); // Valid result
  EXPECT_DOUBLE_EQ(result.value(), 2.5);
}

TEST_F(CgroupCpuUtilTest, ReadActualLimitsV2_EmptyParts) {
  // Test edge case with empty parts after splitting
  CpuFiles cpu_files;
  cpu_files.version = "v2";
  cpu_files.quota_content = "  \n"; // Just whitespace
  cpu_files.period_content = "";

  EXPECT_LOG_CONTAINS("warn", "Malformed cgroup v2 cpu.max", {
    auto result = CgroupCpuUtil::TestUtil::readActualLimitsV2(cpu_files);
    EXPECT_FALSE(result.has_value()); // Malformed returns nullopt
  });
}

} // namespace Envoy
