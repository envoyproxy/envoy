#pragma once

#include <cstdint>

namespace Envoy {
namespace Memory {

class Utils {
public:
  /**
   * Release free memory back to the system.
   * @param max_unfreed_bytes Maximum amount of unfreed memory in bytes to keep.
   *        If 0, uses the default value (100MB). Only used with tcmalloc.
   */
  static void releaseFreeMemory(uint64_t max_unfreed_bytes = 0);
  static void tryShrinkHeap();
};

} // namespace Memory
} // namespace Envoy
