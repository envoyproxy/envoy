#include "source/common/runtime/runtime_features.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Runtime {

// The envoy_cc_test declaration passes
// "--runtime-feature-override-for-tests=envoy.reloadable_features.test_feature_false" and
// "--runtime-feature-disable-for-tests=envoy.reloadable_features.test_feature_true", which flip
// both features away from their defaults for the duration of each test.
TEST(RuntimeFlagForTestsTest, OverrideEnablesFeature) {
  EXPECT_TRUE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));
}

TEST(RuntimeFlagForTestsTest, DisableDisablesFeature) {
  EXPECT_FALSE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_true"));
}

} // namespace Runtime
} // namespace Envoy
