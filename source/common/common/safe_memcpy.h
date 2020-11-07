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
 * @brief Copies src to dst based on the size of dst, which must be specified.
 */
#define safeMemcpySrc(dst, src, size)                                                              \
  do {                                                                                             \
    static_assert(sizeof(*(dst)) == size);                                                         \
    memcpy(dst, src, size);                                                                        \
  } while (0)

/**
 * @brief Copies src to dst based on the size of src, which must be specified.
 */
#define safeMemcpyDst(dst, src, size)                                                              \
  do {                                                                                             \
    static_assert(sizeof(*(src)) == size);                                                         \
    memcpy(dst, src, size);                                                                        \
  } while (0)

} // namespace Envoy
