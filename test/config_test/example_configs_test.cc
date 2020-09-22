#include "test/config_test/config_test.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"
#include "common/filesystem/filesystem_impl.h"

#include "gtest/gtest.h"

namespace Envoy {

// freebind/freebind.yaml is not supported on macOS or Windows and is disabled via Bazel.
constexpr uint64_t LinuxOnlyConfigFileTestCount = 1UL;

TEST(ExampleConfigsTest, All) {
  TestEnvironment::exec(
      {TestEnvironment::runfilesPath("test/config_test/example_configs_test_setup.sh")});

#ifdef WIN32
  Filesystem::InstanceImplWin32 file_system;
#else
  Filesystem::InstanceImplPosix file_system;
#endif

  const auto config_file_count = std::stoi(
      file_system.fileReadToEnd(TestEnvironment::temporaryDirectory() + "/config-file-count.txt"));

  // Change working directory, otherwise we won't be able to read files using relative paths.
#ifdef PATH_MAX
  char cwd[PATH_MAX];
#else
  char cwd[1024];
#endif
  const std::string& directory = TestEnvironment::temporaryDirectory() + "/test/config_test";
  RELEASE_ASSERT(::getcwd(cwd, sizeof(cwd)) != nullptr, "");
  RELEASE_ASSERT(::chdir(directory.c_str()) == 0, "");

#if defined(__APPLE__) || defined(WIN32)
  EXPECT_EQ(config_file_count - LinuxOnlyConfigFileTestCount, ConfigTest::run(directory));
#else
  EXPECT_EQ(config_file_count, ConfigTest::run(directory));
#endif

  ConfigTest::testMerge();

  // Return to the original working directory, otherwise "bazel.coverage" breaks (...but why?).
  RELEASE_ASSERT(::chdir(cwd) == 0, "");
}
} // namespace Envoy
