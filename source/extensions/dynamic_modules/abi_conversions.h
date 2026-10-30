#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

// Returns value narrowed to int, or nullopt when it does not fit. The socket option callbacks use
// this so an out of range int64 level, name, or value is rejected rather than truncated into a
// different option that would then be applied while reporting success.
inline std::optional<int> narrowToInt(int64_t value) {
  if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
    return std::nullopt;
  }
  return static_cast<int>(value);
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
