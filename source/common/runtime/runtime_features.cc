#include "common/runtime/runtime_features.h"

namespace Envoy {
namespace Runtime {

// Add additional features here to enable the new code paths by default.
//
//
// If issues are found that that require a feature to be disabled, it should be reported ASAP by
// filing a bug on github. Overriding non-buggy code is strongly discouraged as once high risk
// features are try by default the old code path will be removed with the next release (and better
// to find and fix bugs while the old code path is still available!)
constexpr const char* runtime_features[] = {
    // Enabled
    "envoy.reloadable_features.test_feature_true",
};

// TODO(alyssawilk) handle deprecation of reloadable_features. Ideally runtime
// override of a deprecated feature will log(warn) on runtime-load if not deprecated
// and hard-fail once it has been deprecated.
constexpr const char* disallowed_features[] = {
    // Acts as both a test entry for deprecated.proto and a marker for the Envoy
    // deprecation scripts.
    "envoy.deprecated_features.deprecated.proto:is_deprecated_fatal",
};

RuntimeFeatures::RuntimeFeatures() {
  for (auto& feature : disallowed_features) {
    disallowed_features_.insert(feature);
  }
  for (auto& feature : runtime_features) {
    enabled_features_.insert(feature);
  }
}

} // namespace Runtime
} // namespace Envoy
