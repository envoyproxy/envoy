#include "test/config_test/config_test.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
TEST(ExampleConfigsTest, All) {
  TestEnvironment::exec(
      {TestEnvironment::runfilesPath("test/config_test/example_configs_test_setup.sh")});
  EXPECT_EQ(26UL, ConfigTest::run(TestEnvironment::temporaryDirectory() + "/test/config_test"));
}
} // namespace Envoy
