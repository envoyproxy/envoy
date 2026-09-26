#pragma once

#include <cstdint>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/dispatcher.h"

#include "source/common/common/thread.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Memory {

constexpr absl::string_view TCMALLOC_ROUTINE_THREAD_ID = "TcmallocProcessBackgroundActions";
constexpr absl::string_view GPERFTOOLS_RELEASE_THREAD_ID = "gperf_release";
constexpr uint64_t DEFAULT_MAX_UNFREED_MEMORY_BYTES = 100 * 1024 * 1024;

/**
 * Accessors for the configurable max unfreed memory threshold. This value controls when
 * tryShrinkHeap releases memory back to the OS. Defaults to 100 MB.
 */
uint64_t maxUnfreedMemoryBytes();
void setMaxUnfreedMemoryBytes(uint64_t value);

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

  /**
   * Get detailed stats about current memory allocation. Returns nullopt if not supported.
   */
  static std::optional<std::string> dumpStats();
};

/**
 * Manages tcmalloc background memory release. With Google's tcmalloc a dedicated thread runs
 * ProcessBackgroundActions. With `gperftools` tcmalloc a dispatcher thread releases the configured
 * number of bytes every interval.
 */
class AllocatorManager {
public:
  AllocatorManager(Api::Api& api,
                   const envoy::config::bootstrap::v3::MemoryAllocatorManager& config);

  ~AllocatorManager();

private:
  const uint64_t bytes_to_release_;
  const std::chrono::milliseconds memory_release_interval_msec_;
  const size_t background_release_rate_bytes_per_second_;
  Api::Api& api_;
  Thread::ThreadPtr tcmalloc_thread_;
  // Declared after the dispatcher so the timer is destroyed first.
  Event::DispatcherPtr tcmalloc_routine_dispatcher_;
  Event::TimerPtr memory_release_timer_;
  void configureBackgroundMemoryRelease();
  void configureTcmallocOptions(const envoy::config::bootstrap::v3::MemoryAllocatorManager& config);
  // Used for testing.
  friend class AllocatorManagerPeer;
};

} // namespace Memory
} // namespace Envoy
