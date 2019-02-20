#pragma once

#include <cstdint>

namespace Envoy {
/**
 * Convert an int based enum to an int.
 */
template <typename T> constexpr uint32_t enumToInt(T val) { return static_cast<uint32_t>(val); }
} // namespace Envoy
