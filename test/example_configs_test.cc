#include "test/config_test.h"
#include "test/test_common/environment.h"

TEST(ExampleConfigsTest, All) {
  TestEnvironment::exec({TestEnvironment::runfilesPath("test/example_configs_test_setup.sh")});
  EXPECT_EQ(8UL, runConfigTest());
}
