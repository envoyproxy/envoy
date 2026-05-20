#include "source/common/memory/stats.h"

#include <atomic>
#include <cstdint>

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#if defined(TCMALLOC)
#include "tcmalloc/malloc_extension.h"
#elif defined(GPERFTOOLS_TCMALLOC)
#include "gperftools/malloc_extension.h"
#elif defined(JEMALLOC)
#include <jemalloc/jemalloc.h>
#endif

namespace Envoy {
namespace Memory {

namespace {
std::atomic<uint64_t> max_unfreed_memory_bytes{DEFAULT_MAX_UNFREED_MEMORY_BYTES};
} // namespace

uint64_t maxUnfreedMemoryBytes() {
  return max_unfreed_memory_bytes.load(std::memory_order_relaxed);
}

void setMaxUnfreedMemoryBytes(uint64_t value) {
  max_unfreed_memory_bytes.store(value, std::memory_order_relaxed);
}

#if defined(JEMALLOC)
namespace {
// Refresh jemalloc's epoch so that subsequently-read stats reflect current state.
void refreshJemallocEpoch() {
  uint64_t epoch = 1;
  size_t sz = sizeof(epoch);
  mallctl("epoch", &epoch, &sz, &epoch, sz);
}
} // namespace
#endif

uint64_t Stats::totalCurrentlyAllocated() {
#if defined(TCMALLOC)
  return tcmalloc::MallocExtension::GetNumericProperty("generic.current_allocated_bytes")
      .value_or(0);
#elif defined(GPERFTOOLS_TCMALLOC)
  size_t value = 0;
  MallocExtension::instance()->GetNumericProperty("generic.current_allocated_bytes", &value);
  return value;
#elif defined(JEMALLOC)
  refreshJemallocEpoch();
  size_t allocated = 0;
  size_t sz = sizeof(allocated);
  mallctl("stats.allocated", &allocated, &sz, nullptr, 0);
  return allocated;
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
#elif defined(JEMALLOC)
  refreshJemallocEpoch();
  size_t mapped = 0;
  size_t sz = sizeof(mapped);
  mallctl("stats.mapped", &mapped, &sz, nullptr, 0);
  return mapped;
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
#elif defined(JEMALLOC)
  // jemalloc uses per-arena caches rather than per-thread caches; no direct equivalent.
  return 0;
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
#elif defined(JEMALLOC)
  refreshJemallocEpoch();
  size_t active = 0, allocated = 0;
  size_t sz = sizeof(size_t);
  mallctl("stats.active", &active, &sz, nullptr, 0);
  mallctl("stats.allocated", &allocated, &sz, nullptr, 0);
  return active > allocated ? active - allocated : 0;
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
#elif defined(JEMALLOC)
  refreshJemallocEpoch();
  size_t retained = 0;
  size_t sz = sizeof(retained);
  mallctl("stats.retained", &retained, &sz, nullptr, 0);
  return retained;
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
#elif defined(JEMALLOC)
  refreshJemallocEpoch();
  size_t resident = 0;
  size_t sz = sizeof(resident);
  mallctl("stats.resident", &resident, &sz, nullptr, 0);
  return resident;
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
#elif defined(JEMALLOC)
  std::string output;
  malloc_stats_print(
      [](void* opaque, const char* msg) { reinterpret_cast<std::string*>(opaque)->append(msg); },
      &output, nullptr);
  ENVOY_LOG_MISC(debug, "jemalloc stats:\n{}", output);
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
#elif defined(JEMALLOC)
  std::string output;
  malloc_stats_print(
      [](void* opaque, const char* msg) { reinterpret_cast<std::string*>(opaque)->append(msg); },
      &output, nullptr);
  return output;
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
  configureTcmallocOptions(config);
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

void AllocatorManager::configureTcmallocOptions(
    const envoy::config::bootstrap::v3::MemoryAllocatorManager& config) {
  if (config.max_unfreed_memory_bytes() > 0) {
    setMaxUnfreedMemoryBytes(config.max_unfreed_memory_bytes());
    ENVOY_LOG_MISC(info, "Set max unfreed memory threshold to {} bytes.",
                   config.max_unfreed_memory_bytes());
  }
#if defined(TCMALLOC)
  if (config.has_soft_memory_limit_bytes()) {
    tcmalloc::MallocExtension::SetMemoryLimit(config.soft_memory_limit_bytes().value(),
                                              tcmalloc::MallocExtension::LimitKind::kSoft);
    ENVOY_LOG_MISC(info, "Set tcmalloc soft memory limit to {} bytes.",
                   config.soft_memory_limit_bytes().value());
  }
  if (config.has_max_per_cpu_cache_size_bytes()) {
    tcmalloc::MallocExtension::SetMaxPerCpuCacheSize(config.max_per_cpu_cache_size_bytes().value());
    ENVOY_LOG_MISC(info, "Set tcmalloc max per-CPU cache size to {} bytes.",
                   config.max_per_cpu_cache_size_bytes().value());
  }
#else
  if (config.has_soft_memory_limit_bytes()) {
    ENVOY_LOG_MISC(warn, "Soft memory limit is only supported with Google's tcmalloc, ignoring.");
  }
  if (config.has_max_per_cpu_cache_size_bytes()) {
    ENVOY_LOG_MISC(warn,
                   "Max per-CPU cache size is only supported with Google's tcmalloc, ignoring.");
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
