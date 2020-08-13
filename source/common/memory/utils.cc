#include "common/memory/utils.h"

#include "envoy/runtime/runtime.h"

#include "common/common/assert.h"
#include "common/memory/stats.h"
#include "common/stats/symbol_table_impl.h"

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
  mainly in xDS config updates, admin handler, and so on. All work on the main thread,
  so the overall impact on performance is small.
  Ref: https://github.com/envoyproxy/envoy/pull/9471#discussion_r363825985
*/
void Utils::tryShrinkHeap() {
#ifdef TCMALLOC
  uint64_t shrink_threshold = 100 * 1024 * 1024UL;
  auto total_physical_bytes = Stats::totalPhysicalBytes();
  auto allocated_size_by_app = Stats::totalCurrentlyAllocated();
  Runtime::Loader* runtime = Runtime::LoaderSingleton::getExisting();
  if (runtime) {
    shrink_threshold = runtime->threadsafeSnapshot()->getInteger(
        "envoy.memory.heap_shrink_threshold", shrink_threshold);
    Envoy::Stats::Scope& root_scope = runtime->getRootScope();

    Envoy::Stats::StatNameManagedStorage shrink_attempt_stat_name("envoy.memory_shrink_attempt",
                                                                  root_scope.symbolTable());

    Envoy::Stats::Counter& shrink_attempt_counter =
        root_scope.counterFromStatName(shrink_attempt_stat_name.statName());
    shrink_attempt_counter.inc();
  }

  ASSERT(total_physical_bytes >= allocated_size_by_app);

  if ((total_physical_bytes - allocated_size_by_app) >= shrink_threshold) {
    Utils::releaseFreeMemory();
    if (runtime) {
      Envoy::Stats::Scope& root_scope = runtime->getRootScope();

      Envoy::Stats::StatNameManagedStorage free_memory_stat_name("envoy.memory_shrink_succeed",
                                                                 root_scope.symbolTable());
      Envoy::Stats::Counter& free_memory_counter =
          root_scope.counterFromStatName(free_memory_stat_name.statName());
      free_memory_counter.inc();
    }
  }
#else
  UNREFERENCED_PARAMETER(threshold);
#endif
}

} // namespace Memory
} // namespace Envoy
