#pragma once

#include <cstdint>

namespace Envoy {
/**
 * Convert an int based enum to an int.
 */
template <typename T> constexpr uint32_t enumToInt(T val) { return static_cast<uint32_t>(val); }

/**
 * Convert an int based enum to a signed int.
 */
template <typename T> constexpr int32_t enumToSignedInt(T val) { return static_cast<int32_t>(val); }
} // namespace Envoy
