#include "source/common/memory/stats.h"

#include <cstdint>

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#if defined(TCMALLOC)

#include "tcmalloc/malloc_extension.h"

namespace Envoy {
namespace Memory {

uint64_t Stats::totalCurrentlyAllocated() {
  return tcmalloc::MallocExtension::GetNumericProperty("generic.current_allocated_bytes")
      .value_or(0);
}

uint64_t Stats::totalCurrentlyReserved() {
  // In Google's tcmalloc the semantics of generic.heap_size has
  // changed: it doesn't include unmapped bytes.
  return tcmalloc::MallocExtension::GetNumericProperty("generic.heap_size").value_or(0) +
         tcmalloc::MallocExtension::GetNumericProperty("tcmalloc.pageheap_unmapped_bytes")
             .value_or(0);
}

uint64_t Stats::totalThreadCacheBytes() {
  return tcmalloc::MallocExtension::GetNumericProperty("tcmalloc.current_total_thread_cache_bytes")
      .value_or(0);
}

uint64_t Stats::totalPageHeapFree() {
  return tcmalloc::MallocExtension::GetNumericProperty("tcmalloc.pageheap_free_bytes").value_or(0);
}

uint64_t Stats::totalPageHeapUnmapped() {
  return tcmalloc::MallocExtension::GetNumericProperty("tcmalloc.pageheap_unmapped_bytes")
      .value_or(0);
}

uint64_t Stats::totalPhysicalBytes() {
  return tcmalloc::MallocExtension::GetProperties()["generic.physical_memory_used"].value;
}

void Stats::dumpStatsToLog() {
  ENVOY_LOG_MISC(debug, "TCMalloc stats:\n{}", tcmalloc::MallocExtension::GetStats());
}

void Allocator::configureBackgroundMemoryRelease(const uint64_t background_release_rate) {
  ASSERT(!tcmalloc_thread_);
  if (background_release_rate > 0) {
    tcmalloc::MallocExtension::SetBackgroundReleaseRate(
        tcmalloc::MallocExtension::BytesPerSecond{background_release_rate});
    ENVOY_LOG_MISC(info, "Configured tcmalloc with background release rate: {} bytes per second",
                   background_release_rate);
    // This routine needs to be invoked for background memory release to be operative.
    // `ProcessBackgroundActions` routine needs to be invoked for background memory release to be
    // operative. https://github.com/google/tcmalloc/blob/master/tcmalloc/malloc_extension.h#L635
    tcmalloc_thread_ = thread_factory_.createThread(
        []() -> void { tcmalloc::MallocExtension::ProcessBackgroundActions(); },
        Thread::Options{"TcmallocProcessBackgroundActions"});
  }
}

} // namespace Memory
} // namespace Envoy

#elif defined(GPERFTOOLS_TCMALLOC)

#include "gperftools/malloc_extension.h"

namespace Envoy {
namespace Memory {

uint64_t Stats::totalCurrentlyAllocated() {
  size_t value = 0;
  MallocExtension::instance()->GetNumericProperty("generic.current_allocated_bytes", &value);
  return value;
}

uint64_t Stats::totalCurrentlyReserved() {
  size_t value = 0;
  MallocExtension::instance()->GetNumericProperty("generic.heap_size", &value);
  return value;
}

uint64_t Stats::totalThreadCacheBytes() {
  size_t value = 0;
  MallocExtension::instance()->GetNumericProperty("tcmalloc.current_total_thread_cache_bytes",
                                                  &value);
  return value;
}

uint64_t Stats::totalPageHeapFree() {
  size_t value = 0;
  MallocExtension::instance()->GetNumericProperty("tcmalloc.pageheap_free_bytes", &value);
  return value;
}

uint64_t Stats::totalPageHeapUnmapped() {
  size_t value = 0;
  MallocExtension::instance()->GetNumericProperty("tcmalloc.pageheap_unmapped_bytes", &value);
  return value;
}

uint64_t Stats::totalPhysicalBytes() {
  size_t value = 0;
  MallocExtension::instance()->GetNumericProperty("generic.total_physical_bytes", &value);
  return value;
}

void Stats::dumpStatsToLog() {
  constexpr int buffer_size = 100000;
  auto buffer = std::make_unique<char[]>(buffer_size);
  MallocExtension::instance()->GetStats(buffer.get(), buffer_size);
  ENVOY_LOG_MISC(debug, "TCMalloc stats:\n{}", buffer.get());
}

}

void configureBackgroundMemoryRelease(const uint64_t background_release_rate) {
  RELEASE_ASSERT(!tcmalloc_thread_);
  if (background_release_rate > 0) {
    MallocExtension::instance()->SetBackgroundReleaseRate(
        MallocExtension::instance()->BytesPerSecond{background_release_rate});
    ENVOY_LOG_MISC(info, "Configured tcmalloc with background release rate: {} bytes per second",
                   background_release_rate);
    // `ProcessBackgroundActions` routine needs to be invoked for background memory release to be
    // operative. https://github.com/google/tcmalloc/blob/master/tcmalloc/malloc_extension.h#L635
    tcmalloc_thread_ = thread_factory_.createThread(
        []() -> void { MallocExtension::instance()->ProcessBackgroundActions(); },
        Thread::Options{"TcmallocProcessBackgroundActions"});
  }
}

} // namespace Memory
} // namespace Envoy

#else

namespace Envoy {
namespace Memory {

uint64_t Stats::totalCurrentlyAllocated() { return 0; }
uint64_t Stats::totalThreadCacheBytes() { return 0; }
uint64_t Stats::totalCurrentlyReserved() { return 0; }
uint64_t Stats::totalPageHeapUnmapped() { return 0; }
uint64_t Stats::totalPageHeapFree() { return 0; }
uint64_t Stats::totalPhysicalBytes() { return 0; }
void Stats::dumpStatsToLog() {}
void Allocator::configureBackgroundMemoryRelease() {}

} // namespace Memory
} // namespace Envoy

#endif // #if defined(TCMALLOC)
