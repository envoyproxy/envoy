#pragma once

#include <cstdint>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

// Returns the worker index encoded in a `worker_{index}` dispatcher name, or zero when the name is
// malformed.
uint32_t parseWorkerIndexFromDispatcherName(absl::string_view dispatcher_name);

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
