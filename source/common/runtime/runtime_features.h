#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Runtime {

class RuntimeFeatures {
public:
  RuntimeFeatures();

  // This tracks config-guarded code paths, to determine if a given
  // runtime-guarded-code-path has the new code run by default or the old code.
  bool enabledByDefault(absl::string_view feature) const {
    return enabled_features_.find(feature) != enabled_features_.end();
  }
  bool existsButDisabled(absl::string_view feature) const {
    return disabled_features_.find(feature) != disabled_features_.end();
  }

private:
  friend class RuntimeFeaturesPeer;

  absl::flat_hash_set<std::string> enabled_features_;
  absl::flat_hash_set<std::string> disabled_features_;
};

using RuntimeFeaturesDefaults = ConstSingleton<RuntimeFeatures>;

} // namespace Runtime
} // namespace Envoy
