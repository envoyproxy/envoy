#include "source/common/runtime/runtime_features.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Runtime {

// Features not in runtime_features.cc are false by default (and this particular one is verified to
// be false in runtime_impl_test.cc). However in in the envoy_cc_test declaration, the flag is set
// "--runtime-feature-override-for-tests=envoy.reloadable_features.test_feature_false"
// to override the return value of runtimeFeatureEnabled to true.
TEST(RuntimeFlagOverrideNoopTest, OverridesNoop) {
  EXPECT_FALSE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));
}

// For features in runtime_features.cc that are true by default, this flag
// "--runtime-feature-override-for-tests=envoy.reloadable_features.test_feature_false" is set in the
// envoy_cc_test declaration to override the return value of runtimeFeatureEnabled to false.
TEST(RuntimeFlagOverrideNoopTest, OverrideDisableFeatureNoop) {
  EXPECT_TRUE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_true"));
}

} // namespace Runtime
} // namespace Envoy
