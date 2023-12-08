#pragma once

#include <cstdint>

#include "source/common/common/thread.h"

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
   * @return uint64_t the amount of memory used by the TCMalloc thread caches (for small objects).
   */
  static uint64_t totalThreadCacheBytes();

  /**
   * @return uint64_t the number of bytes in free, unmapped pages in the page heap. These bytes
   *                  always count towards virtual memory usage, and depending on the OS, typically
   *                  do not count towards physical memory usage.
   */
  static uint64_t totalPageHeapUnmapped();

  /**
   * @return uint64_t the number of bytes in free, mapped pages in the page heap. These bytes always
   *                  count towards virtual memory usage, and unless the underlying memory is
   *                  swapped out by the OS, they also count towards physical memory usage.
   */
  static uint64_t totalPageHeapFree();

  /**
   * @return uint64_t estimate of total bytes of the physical memory usage by the allocator
   */
  static uint64_t totalPhysicalBytes();

  /**
   * Log detailed stats about current memory allocation. Intended for debugging purposes.
   */
  static void dumpStatsToLog();
};

class Allocator {
public:
  Allocator(Thread::ThreadFactory& thread_factory, const uint64_t background_release_rate)
      : thread_factory_(thread_factory), background_release_rate_(background_release_rate) {
    configureBackgroundMemoryRelease();
  };

  ~Allocator();

private:
  Thread::ThreadFactory& thread_factory_;
  const uint64_t background_release_rate_;
  Thread::ThreadPtr tcmalloc_thread_;
  absl::Mutex mutex_;
  bool terminating_ ABSL_GUARDED_BY(mutex_){};
  /**
   * Configures tcmalloc release rate from the page heap. If `background_release_rate_`
   * has been initialized to `0`, no heap memory will be release in background.
   */
  void configureBackgroundMemoryRelease();
  void tcmallocProcessBackgroundActionsThreadRoutine();
};

} // namespace Memory
} // namespace Envoy
