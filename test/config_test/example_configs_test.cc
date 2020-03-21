#include "test/config_test/config_test.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST(ExampleConfigsTest, All) {
  TestEnvironment::exec(
      {TestEnvironment::runfilesPath("test/config_test/example_configs_test_setup.sh")});

  // Change working directory, otherwise we won't be able to read files using relative paths.
#ifdef PATH_MAX
  char cwd[PATH_MAX];
#else
  char cwd[1024];
#endif
  const std::string& directory = TestEnvironment::temporaryDirectory() + "/test/config_test";
  RELEASE_ASSERT(::getcwd(cwd, sizeof(cwd)) != nullptr, "");
  RELEASE_ASSERT(::chdir(directory.c_str()) == 0, "");

#ifdef __APPLE__
  // freebind/freebind.yaml is not supported on macOS and disabled via Bazel.
  EXPECT_EQ(21UL, ConfigTest::run(directory));
#else
  EXPECT_EQ(22UL, ConfigTest::run(directory));
#endif

  ConfigTest::testMerge();

  // Return to the original working directory, otherwise "bazel.coverage" breaks (...but why?).
  RELEASE_ASSERT(::chdir(cwd) == 0, "");
}
} // namespace Envoy
