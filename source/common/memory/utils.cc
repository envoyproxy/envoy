#include "source/common/memory/utils.h"

#include "source/common/common/assert.h"
#include "source/common/memory/stats.h"

#if defined(TCMALLOC)
#include "tcmalloc/malloc_extension.h"
#elif defined(GPERFTOOLS_TCMALLOC)
#include "gperftools/malloc_extension.h"
#endif

namespace Envoy {
namespace Memory {

namespace {
#if defined(TCMALLOC) || defined(GPERFTOOLS_TCMALLOC)
constexpr uint64_t kDefaultMaxUnfreedMemoryBytes = 100 * 1024 * 1024;
#endif
} // namespace

void Utils::releaseFreeMemory(uint64_t max_unfreed_bytes) {
#if defined(TCMALLOC)
  uint64_t threshold = max_unfreed_bytes > 0 ? max_unfreed_bytes : kDefaultMaxUnfreedMemoryBytes;
  tcmalloc::MallocExtension::ReleaseMemoryToSystem(threshold);
#elif defined(GPERFTOOLS_TCMALLOC)
  UNREFERENCED_PARAMETER(max_unfreed_bytes);
  MallocExtension::instance()->ReleaseFreeMemory();
#else
  UNREFERENCED_PARAMETER(max_unfreed_bytes);
#endif
}

/*
  The purpose of this function is to release the cache introduced by tcmalloc,
  mainly in xDS config updates, admin handler, and so on. all work on the main thread,
  so the overall impact on performance is small.
  Ref: https://github.com/envoyproxy/envoy/pull/9471#discussion_r363825985
*/
void Utils::tryShrinkHeap() {
#if defined(TCMALLOC) || defined(GPERFTOOLS_TCMALLOC)
  auto total_physical_bytes = Stats::totalPhysicalBytes();
  auto allocated_size_by_app = Stats::totalCurrentlyAllocated();

  if (total_physical_bytes >= allocated_size_by_app &&
      (total_physical_bytes - allocated_size_by_app) >= kDefaultMaxUnfreedMemoryBytes) {
    Utils::releaseFreeMemory();
  }
#endif
}

} // namespace Memory
} // namespace Envoy
