#include "test/config_test/config_test.h"
#include "test/test_common/environment.h"

TEST(ExampleConfigsTest, All) {
  TestEnvironment::exec(
      {TestEnvironment::runfilesPath("test/config_test/example_configs_test_setup.sh")});
  EXPECT_EQ(8UL, ConfigTest::run(TestEnvironment::temporaryDirectory() + "/test/config_test"));
}
