#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

#include "absl/container/flat_hash_set.h"

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

class RuntimeFeatures {
public:
  RuntimeFeatures() {
    for (auto& feature : disallowed_features) {
      disallowed_features_.insert(feature);
    }
    for (auto& feature : runtime_features) {
      enabled_features_.insert(feature);
    }
  }

  // This tracks proto configured features, to determine if a given deprecated
  // feature is still allowed, or has been made fatal-by-default per the Envoy
  // deprecation process.
  bool disallowedByDefault(const std::string& feature) const {
    return disallowed_features_.find(feature) != disallowed_features_.end();
  }

  // This tracks config-guarded code paths, to determine if a given
  // runtime-guarded-code-path has the new code run by default or the old code.
  bool enabledByDefault(const std::string& feature) const {
    return enabled_features_.find(feature) != enabled_features_.end();
  }

private:
  absl::flat_hash_set<std::string> disallowed_features_;
  absl::flat_hash_set<std::string> enabled_features_;
};

using RuntimeFeaturesDefaults = ConstSingleton<RuntimeFeatures>;

} // namespace Runtime
} // namespace Envoy
