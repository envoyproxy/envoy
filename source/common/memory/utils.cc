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
void Utils::tryShrinkHeap(absl::optional<uint64_t> threshold) {
#ifdef TCMALLOC
  constexpr uint64_t default_unfreed_memory = 100 * 1024 * 1024;
  auto shrink_threshold = threshold.value_or(default_unfreed_memory);
  auto total_physical_bytes = Stats::totalPhysicalBytes();
  auto allocated_size_by_app = Stats::totalCurrentlyAllocated();

  ASSERT(total_physical_bytes >= allocated_size_by_app);

  if ((total_physical_bytes - allocated_size_by_app) >= shrink_threshold) {
    Utils::releaseFreeMemory();
  }
#else
  UNREFERENCED_PARAMETER(threshold);
#endif
}

} // namespace Memory
} // namespace Envoy
