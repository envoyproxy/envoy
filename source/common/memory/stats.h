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
  AllocatorManager(Event::Dispatcher& dispatcher, Thread::ThreadFactory& thread_factory,
                   Envoy::Stats::Scope& scope,
                   const envoy::config::bootstrap::v3::MemoryAllocatorManager& config)
      : thread_factory_(thread_factory), bytes_to_release_(config.bytes_to_release()),
        memory_release_interval_msec_(std::chrono::milliseconds(
            PROTOBUF_GET_MS_OR_DEFAULT(config, memory_release_interval, 1000))),
        allocator_manager_stats_(MemoryAllocatorManagerStats{
            MEMORY_ALLOCATOR_MANAGER_STATS(POOL_COUNTER_PREFIX(scope, "tcmalloc."))}) {
    configureBackgroundMemoryRelease(dispatcher);
  };

  ~AllocatorManager() {
    {
      Thread::LockGuard guard(mutex_);
      terminating_ = true;
      memory_release_event_.notifyOne();
    }
    if (tcmalloc_thread_) {
      tcmalloc_thread_->join();
    }
  }

private:
  Thread::ThreadFactory& thread_factory_;
  const uint64_t bytes_to_release_;
  std::chrono::milliseconds memory_release_interval_msec_;
  MemoryAllocatorManagerStats allocator_manager_stats_;
  Event::TimerPtr memory_release_timer_;
  Thread::ThreadPtr tcmalloc_thread_;
  Thread::MutexBasicLockable mutex_{};
  bool terminating_ ABSL_GUARDED_BY(mutex_){false};
  Thread::CondVar memory_release_event_;
  // Used for testing.
  friend class AllocatorManagerPeer;

  /**
   * Configures tcmalloc release rate from the page heap. If `bytes_to_release_`
   * has been initialized to `0`, no heap memory will be release in background.
   */
  void configureBackgroundMemoryRelease(Event::Dispatcher& dispatcher) {
    RELEASE_ASSERT(!tcmalloc_thread_, "Invalid state, tcmalloc have already been initialised");
    if (bytes_to_release_ > 0) {
      memory_release_timer_ = dispatcher.createTimer([this]() -> void {
        allocator_manager_stats_.released_by_timer_.inc();
        memory_release_event_.notifyOne();
        memory_release_timer_->enableTimer(memory_release_interval_msec_);
      });
      tcmalloc_thread_ = thread_factory_.createThread(
          [this]() -> void { tcmallocProcessBackgroundActionsThreadRoutine(); },
          Thread::Options{"TcmallocProcessBackgroundActions"});
      memory_release_timer_->enableTimer(memory_release_interval_msec_);
      ENVOY_LOG_MISC(
          info,
          fmt::format(
              "Configured tcmalloc with background release rate: {} bytes per {} milliseconds",
              bytes_to_release_, memory_release_interval_msec_.count()));
    }
  }

  void tcmallocProcessBackgroundActionsThreadRoutine();
};

} // namespace Memory
} // namespace Envoy
