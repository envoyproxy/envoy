#include "source/common/memory/stats.h"

#include <cstdint>

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#if defined(TCMALLOC)
#include "tcmalloc/malloc_extension.h"
#elif defined(GPERFTOOLS_TCMALLOC)
#include "gperftools/malloc_extension.h"
#endif

namespace Envoy {
namespace Memory {

uint64_t Stats::totalCurrentlyAllocated() {
#if defined(TCMALLOC)
  return tcmalloc::MallocExtension::GetNumericProperty("generic.current_allocated_bytes")
      .value_or(0);
#elif defined(GPERFTOOLS_TCMALLOC)
  size_t value = 0;
  MallocExtension::instance()->GetNumericProperty("generic.current_allocated_bytes", &value);
  return value;
#else
  return 0;
#endif
}

uint64_t Stats::totalCurrentlyReserved() {
#if defined(TCMALLOC)
  // In Google's tcmalloc the semantics of generic.heap_size has
  // changed: it doesn't include unmapped bytes.
  return tcmalloc::MallocExtension::GetNumericProperty("generic.heap_size").value_or(0) +
         tcmalloc::MallocExtension::GetNumericProperty("tcmalloc.pageheap_unmapped_bytes")
             .value_or(0);
#elif defined(GPERFTOOLS_TCMALLOC)
  size_t value = 0;
  MallocExtension::instance()->GetNumericProperty("generic.heap_size", &value);
  return value;
#else
  return 0;
#endif
}

uint64_t Stats::totalThreadCacheBytes() {
#if defined(TCMALLOC)
  return tcmalloc::MallocExtension::GetNumericProperty("tcmalloc.current_total_thread_cache_bytes")
      .value_or(0);
#elif defined(GPERFTOOLS_TCMALLOC)
  size_t value = 0;
  MallocExtension::instance()->GetNumericProperty("tcmalloc.current_total_thread_cache_bytes",
                                                  &value);
  return value;
#else
  return 0;
#endif
}

uint64_t Stats::totalPageHeapFree() {
#if defined(TCMALLOC)
  return tcmalloc::MallocExtension::GetNumericProperty("tcmalloc.pageheap_free_bytes").value_or(0);
#elif defined(GPERFTOOLS_TCMALLOC)
  size_t value = 0;
  MallocExtension::instance()->GetNumericProperty("tcmalloc.pageheap_free_bytes", &value);
  return value;
#else
  return 0;
#endif
}

uint64_t Stats::totalPageHeapUnmapped() {
#if defined(TCMALLOC)
  return tcmalloc::MallocExtension::GetNumericProperty("tcmalloc.pageheap_unmapped_bytes")
      .value_or(0);
#elif defined(GPERFTOOLS_TCMALLOC)
  size_t value = 0;
  MallocExtension::instance()->GetNumericProperty("tcmalloc.pageheap_unmapped_bytes", &value);
  return value;
#else
  return 0;
#endif
}

uint64_t Stats::totalPhysicalBytes() {
#if defined(TCMALLOC)
  return tcmalloc::MallocExtension::GetProperties()["generic.physical_memory_used"].value;
#elif defined(GPERFTOOLS_TCMALLOC)
  size_t value = 0;
  MallocExtension::instance()->GetNumericProperty("generic.total_physical_bytes", &value);
  return value;
#else
  return 0;
#endif
}

void Stats::dumpStatsToLog() {
#if defined(TCMALLOC)
  ENVOY_LOG_MISC(debug, "TCMalloc stats:\n{}", tcmalloc::MallocExtension::GetStats());
#elif defined(GPERFTOOLS_TCMALLOC)
  constexpr int buffer_size = 100000;
  auto buffer = std::make_unique<char[]>(buffer_size);
  MallocExtension::instance()->GetStats(buffer.get(), buffer_size);
  ENVOY_LOG_MISC(debug, "TCMalloc stats:\n{}", buffer.get());
#else
  return;
#endif
}

AllocatorManager::AllocatorManager(
    Api::Api& api, Envoy::Stats::Scope& scope,
    const envoy::config::bootstrap::v3::MemoryAllocatorManager& config)
    : bytes_to_release_(config.bytes_to_release()),
      memory_release_interval_msec_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(config, memory_release_interval, 1000))),
      allocator_manager_stats_(MemoryAllocatorManagerStats{
          MEMORY_ALLOCATOR_MANAGER_STATS(POOL_COUNTER_PREFIX(scope, "tcmalloc."))}),
      api_(api) {
#if defined(GPERFTOOLS_TCMALLOC)
  if (bytes_to_release_ > 0) {
    ENVOY_LOG_MISC(error,
                   "Memory releasing is not supported for gperf tcmalloc, no memory releasing "
                   "will be configured.");
  }
#elif defined(TCMALLOC)
  configureBackgroundMemoryRelease();
#endif
};

AllocatorManager::~AllocatorManager() {
#if defined(TCMALLOC)
  if (tcmalloc_routine_dispatcher_) {
    tcmalloc_routine_dispatcher_->exit();
  }
  if (tcmalloc_thread_) {
    tcmalloc_thread_->join();
    tcmalloc_thread_.reset();
  }
#endif
}

void AllocatorManager::tcmallocRelease() {
#if defined(TCMALLOC)
  tcmalloc::MallocExtension::ReleaseMemoryToSystem(bytes_to_release_);
#endif
}

/**
 * Configures tcmalloc release rate from the page heap. If `bytes_to_release_`
 * has been initialized to `0`, no heap memory will be released in background.
 */
void AllocatorManager::configureBackgroundMemoryRelease() {
  ENVOY_BUG(!tcmalloc_thread_, "Invalid state, tcmalloc has already been initialised");
  if (bytes_to_release_ > 0) {
    tcmalloc_routine_dispatcher_ = api_.allocateDispatcher(std::string(TCMALLOC_ROUTINE_THREAD_ID));
    memory_release_timer_ = tcmalloc_routine_dispatcher_->createTimer([this]() -> void {
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
    tcmalloc_thread_ = api_.threadFactory().createThread(
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
}

} // namespace Memory
} // namespace Envoy
