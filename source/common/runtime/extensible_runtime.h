#pragma once

#include "envoy/runtime/extensible_runtime.h"

#include "source/common/singleton/threadsafe_singleton.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace ExtensibleRuntime {

// Process-wide singleton holding the active ExtensibleRuntime::Provider configured in bootstrap,
// if any. When no provider is registered, the helper functions below fall back to standard
// Runtime::runtimeFeatureEnabled() evaluation.
using ProviderSingleton = InjectableSingleton<Provider>;

// Check whether a runtime feature is enabled in a process-wide context.
bool runtimeFeatureEnabled(absl::string_view feature);

// Check whether a runtime feature is enabled for the connection associated with stream_info.
bool perConnectionRuntimeFeatureEnabled(absl::string_view feature,
                                        StreamInfo::StreamInfo& stream_info);

// Check whether a runtime feature is enabled for the request/stream associated with stream_info.
bool perRequestRuntimeFeatureEnabled(absl::string_view feature,
                                     StreamInfo::StreamInfo& stream_info);

// Record that stream_info hit a condition where enabling or disabling feature changes behavior.
// No-op if no ExtensibleRuntime::Provider is registered.
void recordBehaviorChange(absl::string_view feature, StreamInfo::StreamInfo& stream_info);

} // namespace ExtensibleRuntime
} // namespace Envoy
