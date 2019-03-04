#include "common/runtime/runtime_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Runtime {

// In the envoy_cc_test declaration, the flag is set
// "--runtime-feature-override-for-tests=envoy.reloadable_features.test_feature_false"
TEST(RuntimeFlagOverrideTest, OverridesWork) {
  // Test the function wrapper suggested in docs.
  EXPECT_TRUE(Runtime::featureEnabled("envoy.reloadable_features.test_feature_false"));

  // Also explicitly make sure the singleton is initialized and configured correctly.
  const Snapshot& snapshot = Runtime::LoaderSingleton::getExisting()->snapshot();
  EXPECT_EQ(true, snapshot.runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));
}

} // namespace Runtime
} // namespace Envoy
