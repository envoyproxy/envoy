#pragma once

#include <cstring>

namespace Envoy {

/**
 * @brief Assert memory bounds to avoid copy errors.
 */
template <typename T1, typename T2> inline void safeMemcpy(T1* dst, T2* src) {
  static_assert(sizeof(T1) == sizeof(T2));
  memcpy(dst, src, sizeof(T1));
}

} // namespace Envoy
