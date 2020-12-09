#pragma once

#include <cstring>

namespace Envoy {

/**
 * @brief Copies src to dst based on their sizes, which must be the same.
 */
template <typename T1, typename T2> inline void safeMemcpy(T1* dst, T2* src) {
  static_assert(sizeof(T1) == sizeof(T2));
  memcpy(dst, src, sizeof(T1));
}

/**
 * @brief Copies src to dst based on the size of dst
 * @note  Sizes are not compared, so ensure the src is of size sizeof(*(dst)) before proceeding to
 * call safeMemcpyUnsafeSrc
 */
template <typename T1> inline void safeMemcpyUnsafeSrc(T1* dst, void* src) {
  memcpy(dst, src, sizeof(T1);
}
/**
 * @brief Copies src to dst based on the size of src
 * @note  Sizes are not compared, so ensure the dst is of size sizeof(*(src)) before proceeding to
 * call safeMemcpyUnsafeDst
 */
template <typename T2> inline void safeMemcpyUnsafeDst(void* dst, T2* src) {
  memcpy(dst, src, sizeof(T2));
}

} // namespace Envoy
