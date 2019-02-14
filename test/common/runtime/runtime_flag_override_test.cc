#include "common/runtime/runtime_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Runtime {

// In the envoy_cc_test declaration, the flag is set
// "--runtime-feature-override-for-tests=envoy.reloadable_features.test_feature_false"
TEST(RuntimeFlagOverrideTest, OverridesWork) {
  Snapshot& snapshot = Runtime::LoaderSingleton::getExisting()->snapshot();
  EXPECT_EQ(true, snapshot.runtimeFeatureEnabled("envoy.reloadable_features.test_feature_false"));
}

} // namespace Runtime
} // namespace Envoy
