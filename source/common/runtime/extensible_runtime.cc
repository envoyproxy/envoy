#include "source/common/runtime/extensible_runtime.h"

#include "envoy/runtime/extensible_runtime.h"

#include "source/common/runtime/runtime_features.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace ExtensibleRuntime {

bool runtimeFeatureEnabled(absl::string_view feature) {
  Provider* provider = ProviderSingleton::getExisting();
  if (provider != nullptr) {
    return provider->runtimeFeatureEnabled(feature);
  }
  return Runtime::runtimeFeatureEnabled(feature);
}

bool perConnectionRuntimeFeatureEnabled(absl::string_view feature,
                                        StreamInfo::StreamInfo& stream_info) {
  Provider* provider = ProviderSingleton::getExisting();
  if (provider != nullptr) {
    return provider->perConnectionRuntimeFeatureEnabled(feature, stream_info);
  }
  return Runtime::runtimeFeatureEnabled(feature);
}

bool perRequestRuntimeFeatureEnabled(absl::string_view feature,
                                     StreamInfo::StreamInfo& stream_info) {
  Provider* provider = ProviderSingleton::getExisting();
  if (provider != nullptr) {
    return provider->perRequestRuntimeFeatureEnabled(feature, stream_info);
  }
  return Runtime::runtimeFeatureEnabled(feature);
}

void recordBehaviorChange(absl::string_view feature, StreamInfo::StreamInfo& stream_info) {
  Provider* provider = ProviderSingleton::getExisting();
  if (provider != nullptr) {
    provider->recordBehaviorChange(feature, stream_info);
  }
}

} // namespace ExtensibleRuntime
} // namespace Envoy
