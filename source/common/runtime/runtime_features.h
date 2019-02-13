#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Runtime {

const char* disallowed_features[] = {
    // Acts as both a test entry for deprecated.proto and a marker for the Envoy
    // deprecation scripts.
    "envoy.deprecated_features.deprecated.proto:is_deprecated_fatal",
};

class DisallowedFeatures {
public:
  DisallowedFeatures() {
    for (auto& feature : disallowed_features) {
      disallowed_features_.insert(feature);
    }
  }

  bool disallowedByDefault(const std::string& feature) const {
    return disallowed_features_.find(feature) != disallowed_features_.end();
  }

private:
  absl::flat_hash_set<std::string> disallowed_features_;
};

using DisallowedFeaturesDefaults = ConstSingleton<DisallowedFeatures>;

} // namespace Runtime
} // namespace Envoy
