#pragma once

#include "envoy/runtime/extensible_runtime.h"

#include "source/common/singleton/threadsafe_singleton.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace ExtensibleRuntime {

// Process-wide singleton holding the active ExtensibleRuntime::Provider configured in bootstrap,
// if any. When a provider is registered, the functions below evaluate features through it;
// otherwise they fall back to Runtime::runtimeFeatureEnabled().
//
// Because a provider's state can change during a connection or request, repeated calls with the
// same StreamInfo are not guaranteed to return the same value. As with
// Runtime::runtimeFeatureEnabled(), callers that need the same value across multiple calls should
// evaluate the feature once and store the result.
using ProviderSingleton = InjectableSingleton<Provider>;

// Evaluate whether a runtime feature is enabled without connection or request context.
bool runtimeFeatureEnabled(absl::string_view feature);

// Evaluate whether a connection-level runtime feature is enabled.
bool connectionRuntimeFeatureEnabled(absl::string_view feature,
                                     const StreamInfo::StreamInfo& stream_info);

// Evaluate whether a request-level runtime feature is enabled.
bool requestRuntimeFeatureEnabled(absl::string_view feature,
                                  const StreamInfo::StreamInfo& stream_info);

} // namespace ExtensibleRuntime
} // namespace Envoy
