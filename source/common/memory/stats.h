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
                   const envoy::config::bootstrap::v3::MemoryAllocatorManager& config)
      : bytes_to_release_(config.bytes_to_release()),
        memory_release_interval_msec_(std::chrono::milliseconds(
            PROTOBUF_GET_MS_OR_DEFAULT(config, memory_release_interval, 1000))),
        allocator_manager_stats_(MemoryAllocatorManagerStats{
            MEMORY_ALLOCATOR_MANAGER_STATS(POOL_COUNTER_PREFIX(scope, "tcmalloc."))}) {
    configureBackgroundMemoryRelease(api);
  };

  ~AllocatorManager() {
    {
      Thread::LockGuard guard(mutex_);
      terminating_ = true;
    }
    tcmalloc_routine_dispatcher_->exit();
    if (tcmalloc_thread_) {
      tcmalloc_thread_->join();
      tcmalloc_thread_.reset();
    }
  }

private:
  const uint64_t bytes_to_release_;
  const std::chrono::milliseconds memory_release_interval_msec_;
  MemoryAllocatorManagerStats allocator_manager_stats_;
  Event::TimerPtr memory_release_timer_;
  Thread::ThreadPtr tcmalloc_thread_;
  Event::DispatcherPtr tcmalloc_routine_dispatcher_;
  Thread::MutexBasicLockable mutex_{};
  bool terminating_ ABSL_GUARDED_BY(mutex_){false};
  // Used for testing.
  friend class AllocatorManagerPeer;

  /**
   * Configures tcmalloc release rate from the page heap. If `bytes_to_release_`
   * has been initialized to `0`, no heap memory will be release in background.
   */
  void configureBackgroundMemoryRelease(Api::Api& api) {
    RELEASE_ASSERT(!tcmalloc_thread_, "Invalid state, tcmalloc have already been initialised");
    tcmalloc_routine_dispatcher_ = api.allocateDispatcher(std::string(TCMALLOC_ROUTINE_THREAD_ID));
    memory_release_timer_ = tcmalloc_routine_dispatcher_->createTimer([this]() -> void {
      Thread::ReleasableLockGuard guard(mutex_);
      if (terminating_) {
        guard.release();
        memory_release_timer_->disableTimer();
        return;
      }
      guard.release();
      const uint64_t unmapped_bytes_before_release = Stats::totalPageHeapUnmapped();
      tcmallocRelease();
      const uint64_t unmapped_bytes_after_release = Stats::totalPageHeapUnmapped();
      if (unmapped_bytes_after_release > unmapped_bytes_before_release) {
        // Only increment stats if memory was actually released. As tcmalloc releases memory on a
        // span granularity, during some release rounds there may be no memory released, if during
        // past round too much memory was released.
        // https://github.com/google/tcmalloc/blob/master/tcmalloc/tcmalloc.cc#L298
        allocator_manager_stats_.released_by_timer_.inc();
      }
      memory_release_timer_->enableTimer(memory_release_interval_msec_);
    });
    tcmalloc_thread_ = api.threadFactory().createThread(
        [this]() -> void {
          ENVOY_LOG_MISC(debug, "Started {}", TCMALLOC_ROUTINE_THREAD_ID);
          memory_release_timer_->enableTimer(memory_release_interval_msec_);
          tcmalloc_routine_dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
        },
        Thread::Options{std::string(TCMALLOC_ROUTINE_THREAD_ID)});
    ENVOY_LOG_MISC(
        info, fmt::format(
                  "Configured tcmalloc with background release rate: {} bytes per {} milliseconds",
                  bytes_to_release_, memory_release_interval_msec_.count()));
  }

  void tcmallocRelease();
};

} // namespace Memory
} // namespace Envoy
