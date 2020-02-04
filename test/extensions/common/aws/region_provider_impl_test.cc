#include "extensions/common/aws/region_provider_impl.h"

#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class EnvironmentRegionProviderTest : public testing::Test {
public:
  ~EnvironmentRegionProviderTest() override { TestEnvironment::unsetEnvVar("AWS_REGION"); }

  EnvironmentRegionProvider provider_;
};

class StaticRegionProviderTest : public testing::Test {
public:
  StaticRegionProviderTest() : provider_("test-region") {}

  StaticRegionProvider provider_;
};

TEST_F(EnvironmentRegionProviderTest, SomeRegion) {
  TestEnvironment::setEnvVar("AWS_REGION", "test-region", 1);
  EXPECT_EQ("test-region", provider_.getRegion().value());
}

TEST_F(EnvironmentRegionProviderTest, NoRegion) { EXPECT_FALSE(provider_.getRegion().has_value()); }

TEST_F(StaticRegionProviderTest, SomeRegion) {
  EXPECT_EQ("test-region", provider_.getRegion().value());
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
