#pragma once

#include <cstring>

namespace Envoy {

/**
 * Copies src to dst based on their sizes, which must be the same.
 * @param dst the destination
 * @param src the source
 */
template <typename T1, typename T2> inline void safeMemcpy(T1* dst, T2* src) {
  static_assert(sizeof(T1) == sizeof(T2));
  memcpy(dst, src, sizeof(T1));
}

/**
 * Copies src to dst based on the size of dst.
 * Sizes are not compared, so ensure the src is of size sizeof(*(dst)) before proceeding to
 * call safeMemcpyUnsafeSrc
 * @param dst the destination
 * @param src the source
 */
template <typename T1, typename T2> inline void safeMemcpyUnsafeSrc(T1* dst, T2* src) {
  memcpy(dst, src, sizeof(T1));
}
/**
 * Copies src to dst based on the size of src.
 * Sizes are not compared, so ensure the dst is of size sizeof(*(src)) before proceeding to
 * call safeMemcpyUnsafeDst
 * @param dst the destination
 * @param src the source
 */
template <typename T1, typename T2> inline void safeMemcpyUnsafeDst(T1* dst, T2* src) {
  memcpy(dst, src, sizeof(T2));
}

} // namespace Envoy
