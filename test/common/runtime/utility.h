#pragma once

#include "common/runtime/runtime_features.h"
#include "common/runtime/runtime_impl.h"

namespace Envoy {
namespace Runtime {

class RuntimeFeaturesPeer {
public:
  static bool addFeature(const std::string& feature, bool disable) {
    if (disable) {
      // Remove from enabled features and add to disabled features.
      bool found = const_cast<Runtime::RuntimeFeatures*>(&Runtime::RuntimeFeaturesDefaults::get())
                       ->enabledByDefault(feature);
      const_cast<Runtime::RuntimeFeatures*>(&Runtime::RuntimeFeaturesDefaults::get())
          ->enabled_features_.erase(feature);
      const_cast<Runtime::RuntimeFeatures*>(&Runtime::RuntimeFeaturesDefaults::get())
          ->disabled_features_.insert(feature);
      return found;
    } else {
      return const_cast<Runtime::RuntimeFeatures*>(&Runtime::RuntimeFeaturesDefaults::get())
          ->enabled_features_.insert(feature)
          .second;
    }
  }
  static void removeFeature(const std::string& feature, bool disable) {
    if (disable) {
      // Remove from disabled features and add to enabled features as set originally.
      const_cast<Runtime::RuntimeFeatures*>(&Runtime::RuntimeFeaturesDefaults::get())
          ->disabled_features_.erase(feature);
      const_cast<Runtime::RuntimeFeatures*>(&Runtime::RuntimeFeaturesDefaults::get())
          ->enabled_features_.insert(feature);
    } else {
      const_cast<Runtime::RuntimeFeatures*>(&Runtime::RuntimeFeaturesDefaults::get())
          ->enabled_features_.erase(feature);
    }
  }
};

} // namespace Runtime
} // namespace Envoy
