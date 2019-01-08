#include "common/aws/region_provider_impl.h"

#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Aws {
namespace Auth {

class EnvironmentRegionProviderTest : public testing::Test {
public:
  virtual ~EnvironmentRegionProviderTest() { TestEnvironment::unsetEnvVar("AWS_REGION"); }

  EnvironmentRegionProvider provider_;
};

TEST_F(EnvironmentRegionProviderTest, SomeRegion) {
  TestEnvironment::setEnvVar("AWS_REGION", "test-region", 1);
  EXPECT_EQ("test-region", provider_.getRegion().value());
}

TEST_F(EnvironmentRegionProviderTest, NoRegion) { EXPECT_FALSE(provider_.getRegion().has_value()); }

} // namespace Auth
} // namespace Aws
} // namespace Envoy