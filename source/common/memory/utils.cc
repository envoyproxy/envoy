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
// TODO(zyfjeff): Make max unfreed memory byte configurable
constexpr uint64_t MAX_UNFREED_MEMORY_BYTE = 100 * 1024 * 1024;
#endif
} // namespace

void Utils::releaseFreeMemory() {
#if defined(TCMALLOC)
  tcmalloc::MallocExtension::ReleaseMemoryToSystem(MAX_UNFREED_MEMORY_BYTE);
#elif defined(GPERFTOOLS_TCMALLOC)
  MallocExtension::instance()->ReleaseFreeMemory();
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
      (total_physical_bytes - allocated_size_by_app) >= MAX_UNFREED_MEMORY_BYTE) {
    Utils::releaseFreeMemory();
  }
#endif
}

} // namespace Memory
} // namespace Envoy
