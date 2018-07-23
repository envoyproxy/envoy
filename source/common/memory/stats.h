#pragma once

#include <cstdint>

namespace Envoy {
namespace Memory {

/**
 * Runtime stats for process memory usage.
 */
class Stats {
public:
  /**
   * @return uint64_t the total memory currently allocated.
   */
  static uint64_t totalCurrentlyAllocated();

  /**
   * @return uint64_t the total memory reserved for the process by the heap but not necessarily
   *                  allocated.
   */
  static uint64_t totalCurrentlyReserved();

  /**
   * @return uint64_t the number of bytes in free, unmapped pages in the page heap.
   */
  static uint64_t totalPageHeapUnmapped();
};

} // namespace Memory
} // namespace Envoy
