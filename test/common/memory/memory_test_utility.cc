#include "test/common/memory/memory_test_utility.h"

namespace Envoy {
namespace Memory {
namespace TestUtil {

MemoryTest::Mode MemoryTest::mode() {
#if !(defined(TCMALLOC) || defined(GPERFTOOLS_TCMALLOC)) || defined(ENVOY_MEMORY_DEBUG_ENABLED)
  // We can only test absolute memory usage if the malloc library is a known
  // quantity. This decision is centralized here. As the preferred malloc
  // library for Envoy is TCMALLOC that's what we test for here. If we switch
  // to a different malloc library than we'd have to re-evaluate all the
  // thresholds in the tests referencing MemoryTest.
  return Mode::Disabled;
#else
  // Even when using TCMALLOC is defined, it appears that
  // Memory::Stats::totalCurrentlyAllocated() does not work as expected
  // on some platforms, so try to force-allocate some heap memory
  // and determine whether we can measure it.
  const size_t start_mem = Memory::Stats::totalCurrentlyAllocated();
  volatile std::unique_ptr<std::string> long_string = std::make_unique<std::string>(
      "more than 22 chars to exceed libc++ short-string optimization");
  const size_t end_mem = Memory::Stats::totalCurrentlyAllocated();
  bool can_measure_memory = end_mem > start_mem;

  // As of Oct 8, 2020, tcmalloc has changed such that Memory::Stats::totalCurrentlyAllocated
  // is not deterministic, even with single-threaded tests. When possible, this should be fixed,
  // and the following block of code uncommented. This affects approximate comparisons, not
  // just exact ones.
#if 0
  if (getenv("ENVOY_MEMORY_TEST_EXACT") != nullptr) { // Set in "ci/do_ci.sh" for 'release' tests.
    RELEASE_ASSERT(can_measure_memory,
                   "$ENVOY_MEMORY_TEST_EXACT is set for canonical memory measurements, "
                   "but memory measurement looks broken");
    return Mode::Canonical;
  }
#endif

  // Different versions of STL and other compiler/architecture differences may
  // also impact memory usage, so when not compiling with MEMORY_TEST_EXACT,
  // memory comparisons must be given some slack. There have recently emerged
  // some memory-allocation differences between development and Envoy CI and
  // Bazel CI (which compiles Envoy as a test of Bazel).
  return can_measure_memory ? Mode::Approximate : Mode::Disabled;
#endif
}

} // namespace TestUtil
} // namespace Memory
} // namespace Envoy
