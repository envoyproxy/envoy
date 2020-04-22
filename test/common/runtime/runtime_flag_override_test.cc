#include "common/runtime/runtime_features.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Runtime {

// Features not in runtime_features.cc are false by default (and this particular one is verified to
// be false in runtime_impl_test.cc). However in in the envoy_cc_test declaration, the flag is set
// "--runtime-feature-override-for-tests=envoy.reloadable_features.test_feature_false"
// to override the return value of runtimeFeatureEnabled to true.
TEST(RuntimeFlagOverrideTest, OverridesWork) {
  EXPECT_TRUE(Runtime::runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));
}

} // namespace Runtime
} // namespace Envoy
