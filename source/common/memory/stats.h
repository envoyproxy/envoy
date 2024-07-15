#pragma once

#include <cstdint>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/stats/store.h"

#include "source/common/common/thread.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {

#define MEMORY_ALLOCATOR_MANAGER_STATS(COUNTER) COUNTER(released_by_timer)

struct MemoryAllocatorManagerStats {
  MEMORY_ALLOCATOR_MANAGER_STATS(GENERATE_COUNTER_STRUCT)
};

namespace Memory {

constexpr absl::string_view TCMALLOC_ROUTINE_THREAD_ID = "TcmallocProcessBackgroundActions";

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

class AllocatorManager {
public:
  AllocatorManager(Api::Api& api, Envoy::Stats::Scope& scope,
                   const envoy::config::bootstrap::v3::MemoryAllocatorManager& config);

  ~AllocatorManager();

private:
  const uint64_t bytes_to_release_;
  const std::chrono::milliseconds memory_release_interval_msec_;
  MemoryAllocatorManagerStats allocator_manager_stats_;
  Api::Api& api_;
  Thread::ThreadPtr tcmalloc_thread_;
  Event::DispatcherPtr tcmalloc_routine_dispatcher_;
  Event::TimerPtr memory_release_timer_;
  void configureBackgroundMemoryRelease();
  void tcmallocRelease();
  // Used for testing.
  friend class AllocatorManagerPeer;
};

} // namespace Memory
} // namespace Envoy
