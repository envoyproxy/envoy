#pragma once

#include "envoy/runtime/extensible_runtime.h"

#include "source/common/singleton/threadsafe_singleton.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace ExtensibleRuntime {

// Process-wide singleton holding the active ExtensibleRuntime::Provider configured in bootstrap,
// if any. When a provider is registered, the functions below delegate to it; otherwise they fall
// back to Runtime::runtimeFeatureEnabled().
using ProviderSingleton = InjectableSingleton<Provider>;

// Evaluate whether a runtime feature is enabled.
bool runtimeFeatureEnabled(absl::string_view feature);

// Evaluate whether a runtime feature is enabled given connection or stream context.
bool runtimeFeatureEnabled(absl::string_view feature, const StreamInfo::StreamInfo& stream_info);

} // namespace ExtensibleRuntime
} // namespace Envoy
