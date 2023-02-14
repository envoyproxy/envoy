#pragma once

#include <string>

#include "envoy/runtime/runtime.h"

#include "source/common/singleton/const_singleton.h"

#include "absl/container/flat_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/flags/reflection.h"

namespace Envoy {
namespace Runtime {

bool hasRuntimePrefix(absl::string_view feature);
bool isRuntimeFeature(absl::string_view feature);
bool runtimeFeatureEnabled(absl::string_view feature);
uint64_t getInteger(absl::string_view feature, uint64_t default_value);

void markRuntimeInitialized();
bool isRuntimeInitialized();

void maybeSetRuntimeGuard(absl::string_view name, bool value);

void maybeSetDeprecatedInts(absl::string_view name, uint32_t value);
constexpr absl::string_view defer_processing_backedup_streams =
    "envoy.reloadable_features.defer_processing_backedup_streams";

} // namespace Runtime
} // namespace Envoy
