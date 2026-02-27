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

absl::optional<std::string> Stats::dumpStats() {
#if defined(TCMALLOC)
  return tcmalloc::MallocExtension::GetStats();
#elif defined(GPERFTOOLS_TCMALLOC)
  constexpr int buffer_size = 100000;
  std::string buffer(buffer_size, '\0');
  MallocExtension::instance()->GetStats(buffer.data(), buffer_size);
  buffer.resize(strlen(buffer.c_str()));
  return buffer;
#else
  return absl::nullopt;
#endif
}

namespace {

/**
 * Computes the background release rate in bytes per second from the configured bytes_to_release
 * and memory_release_interval.
 */
size_t computeBackgroundReleaseRate(uint64_t bytes_to_release,
                                    std::chrono::milliseconds memory_release_interval_msec) {
  if (bytes_to_release == 0 || memory_release_interval_msec.count() == 0) {
    return 0;
  }
  return static_cast<size_t>(bytes_to_release * 1000 / memory_release_interval_msec.count());
}

} // namespace

AllocatorManager::AllocatorManager(
    Api::Api& api, const envoy::config::bootstrap::v3::MemoryAllocatorManager& config)
    : bytes_to_release_(config.bytes_to_release()),
      memory_release_interval_msec_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(config, memory_release_interval, 1000))),
      background_release_rate_bytes_per_second_(
          computeBackgroundReleaseRate(bytes_to_release_, memory_release_interval_msec_)),
      api_(api) {
  configureBackgroundMemoryRelease();
};

AllocatorManager::~AllocatorManager() {
#if defined(TCMALLOC)
  if (tcmalloc_thread_) {
    // Signal the ProcessBackgroundActions loop to exit and wait for the thread to finish.
    tcmalloc::MallocExtension::SetBackgroundProcessActionsEnabled(false);
    tcmalloc_thread_->join();
    tcmalloc_thread_.reset();
    // Reset the release rate and re-enable background actions so that a subsequent
    // AllocatorManager instance can start fresh.
    tcmalloc::MallocExtension::SetBackgroundReleaseRate(
        tcmalloc::MallocExtension::BytesPerSecond{0});
    tcmalloc::MallocExtension::SetBackgroundProcessActionsEnabled(true);
  }
#endif
}

/**
 * Configures tcmalloc to use its native ProcessBackgroundActions for background memory
 * maintenance. This enables comprehensive memory management including per-CPU cache reclamation,
 * cache shuffling, size class resizing, transfer cache plundering, and memory release at the
 * configured rate. If `bytes_to_release_` is `0`, no background processing will be started.
 */
void AllocatorManager::configureBackgroundMemoryRelease() {
#if defined(GPERFTOOLS_TCMALLOC)
  if (bytes_to_release_ > 0) {
    ENVOY_LOG_MISC(error,
                   "Memory releasing is not supported for gperf tcmalloc, no memory releasing "
                   "will be configured.");
  }
#elif defined(TCMALLOC)
  ENVOY_BUG(!tcmalloc_thread_, "Invalid state, tcmalloc has already been initialised.");
  if (bytes_to_release_ > 0) {
    if (!tcmalloc::MallocExtension::NeedsProcessBackgroundActions()) {
      ENVOY_LOG_MISC(warn, "This platform does not support tcmalloc background actions.");
      return;
    }

    tcmalloc::MallocExtension::SetBackgroundReleaseRate(
        tcmalloc::MallocExtension::BytesPerSecond{background_release_rate_bytes_per_second_});

    tcmalloc_thread_ = api_.threadFactory().createThread(
        []() -> void {
          ENVOY_LOG_MISC(debug, "Started {}.", TCMALLOC_ROUTINE_THREAD_ID);
          // ProcessBackgroundActions runs an infinite loop that handles all tcmalloc background
          // maintenance including cache reclamation and memory release. It returns only when
          // SetBackgroundProcessActionsEnabled(false) is called.
          tcmalloc::MallocExtension::ProcessBackgroundActions();
        },
        Thread::Options{std::string(TCMALLOC_ROUTINE_THREAD_ID)});

    ENVOY_LOG_MISC(info, "Configured tcmalloc with background release rate: {} bytes per second.",
                   background_release_rate_bytes_per_second_);
  }
#endif
}

} // namespace Memory
} // namespace Envoy
