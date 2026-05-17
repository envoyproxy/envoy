#include "source/common/memory/utils.h"

#include "source/common/common/assert.h"
#include "source/common/memory/stats.h"

#if defined(TCMALLOC)
#include "tcmalloc/malloc_extension.h"
#elif defined(GPERFTOOLS_TCMALLOC)
#include "gperftools/malloc_extension.h"
#elif defined(JEMALLOC)
#include <jemalloc/jemalloc.h>

#include <cstdio>
#endif

namespace Envoy {
namespace Memory {

void Utils::releaseFreeMemory(uint64_t max_unfreed_bytes) {
#if defined(TCMALLOC)
  uint64_t threshold = max_unfreed_bytes > 0 ? max_unfreed_bytes : maxUnfreedMemoryBytes();
  tcmalloc::MallocExtension::ReleaseMemoryToSystem(threshold);
#elif defined(GPERFTOOLS_TCMALLOC)
  UNREFERENCED_PARAMETER(max_unfreed_bytes);
  MallocExtension::instance()->ReleaseFreeMemory();
#elif defined(JEMALLOC)
  UNREFERENCED_PARAMETER(max_unfreed_bytes);
  // Purge all arenas to release dirty pages back to the OS.
  // `MALLCTL_ARENAS_ALL` is jemalloc's pseudo-index for addressing all arenas at once.
  char purge_cmd[32];
  snprintf(purge_cmd, sizeof(purge_cmd), "arena.%u.purge", MALLCTL_ARENAS_ALL);
  mallctl(purge_cmd, nullptr, nullptr, nullptr, 0);
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
#if defined(TCMALLOC) || defined(GPERFTOOLS_TCMALLOC) || defined(JEMALLOC)
  auto total_physical_bytes = Stats::totalPhysicalBytes();
  auto allocated_size_by_app = Stats::totalCurrentlyAllocated();
  const uint64_t threshold = maxUnfreedMemoryBytes();

  if (total_physical_bytes >= allocated_size_by_app &&
      (total_physical_bytes - allocated_size_by_app) >= threshold) {
    Utils::releaseFreeMemory();
  }
#endif
}

} // namespace Memory
} // namespace Envoy
