#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Runtime {

const char* disallowed_features[] = {
    // Acts as both a test entry for deprecated.proto and a marker for the Envoy
    // deprecation scripts.
    "is_deprecated_fatal",
};

class DisallowedFeatures {
public:
  DisallowedFeatures() {
    for (uint32_t i = 0; i < ABSL_ARRAYSIZE(disallowed_features); ++i) {
      disallowed_features_.insert(absl::StrCat(Prefix, disallowed_features[i]));
    }
  }

  bool disallowedByDefault(const std::string& feature) const {
    return disallowed_features_.find(feature) != disallowed_features_.end();
  }

  // We prefix deprecated features with this to do best-effort avoidance of
  // upstream runtime variables.
  const std::string Prefix{"envoy.deprecated_feature."};

private:
  absl::flat_hash_set<std::string> disallowed_features_;
};

typedef ConstSingleton<DisallowedFeatures> DisallowedFeaturesDefaults;

} // namespace Runtime
} // namespace Envoy
