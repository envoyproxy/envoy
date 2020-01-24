#pragma once

#include "common/runtime/runtime_features.h"
#include "common/runtime/runtime_impl.h"

namespace Envoy {
namespace Runtime {

class RuntimeFeaturesPeer {
public:
  static bool addFeature(const std::string& feature) {
    return const_cast<Runtime::RuntimeFeatures*>(&Runtime::RuntimeFeaturesDefaults::get())
        ->enabled_features_.insert(feature)
        .second;
  }
  static void removeFeature(const std::string& feature) {
    const_cast<Runtime::RuntimeFeatures*>(&Runtime::RuntimeFeaturesDefaults::get())
        ->enabled_features_.erase(feature);
  }
};

} // namespace Runtime
} // namespace Envoy
