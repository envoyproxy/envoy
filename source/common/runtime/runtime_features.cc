#include "common/runtime/runtime_features.h"

namespace Envoy {
namespace Runtime {

// Add additional features here to enable the new code paths by default.
//
// These features should not be overridden with run-time guards without a bug
// being filed on github as once high risk features are true by default, the
// old code path will be removed with the next release.
const char* runtime_features[] = {
    // Enabled
    "envoy.reloadable_features.test_feature_true",
    // Disabled
    // "envoy.reloadable_features.test_feature_false",
};

// TODO(alyssawilk) handle deprecation of reloadable_features. Ideally runtime
// override of a deprecated feature will log(warn) on runtime-load if not deprecated
// and hard-fail once it has been deprecated.
const char* disallowed_features[] = {
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
