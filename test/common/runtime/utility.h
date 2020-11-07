#pragma once

#include "common/runtime/runtime_features.h"
#include "common/runtime/runtime_impl.h"

namespace Envoy {
namespace Runtime {

class RuntimeFeaturesPeer {
public:
  static bool enableFeature(const std::string& feature) {
    // Remove from disabled features and add to enabled features.
    const_cast<Runtime::RuntimeFeatures*>(&Runtime::RuntimeFeaturesDefaults::get())
        ->disabled_features_.erase(feature);
    return const_cast<Runtime::RuntimeFeatures*>(&Runtime::RuntimeFeaturesDefaults::get())
        ->enabled_features_.insert(feature)
        .second;
  }
  static bool disableFeature(const std::string& feature) {
    // Remove from enabled features and add to disabled features.
    const_cast<Runtime::RuntimeFeatures*>(&Runtime::RuntimeFeaturesDefaults::get())
        ->enabled_features_.erase(feature);
    return const_cast<Runtime::RuntimeFeatures*>(&Runtime::RuntimeFeaturesDefaults::get())
        ->disabled_features_.insert(feature)
        .second;
  }
};

} // namespace Runtime
} // namespace Envoy
