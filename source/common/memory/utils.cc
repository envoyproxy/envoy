#include "common/memory/utils.h"

#include "common/common/assert.h"
#include "common/memory/stats.h"

#ifdef TCMALLOC
#include "gperftools/malloc_extension.h"
#endif

namespace Envoy {
namespace Memory {

void Utils::releaseFreeMemory() {
#ifdef TCMALLOC
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
#ifdef TCMALLOC
  // TODO(zyfjeff): Make max unfreed memory byte configurable
  static const uint64_t MAX_UNFREED_MEMORY_BYTE = 100 * 1024 * 1024;
  auto total_physical_bytes = Stats::totalPhysicalBytes();
  auto allocated_size_by_app = Stats::totalCurrentlyAllocated();

  ASSERT(total_physical_bytes >= allocated_size_by_app);

  if ((total_physical_bytes - allocated_size_by_app) >= MAX_UNFREED_MEMORY_BYTE) {
    Utils::releaseFreeMemory();
  }
#endif
}

} // namespace Memory
} // namespace Envoy
