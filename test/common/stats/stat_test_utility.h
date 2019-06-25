#pragma once

#include "common/common/logger.h"
#include "common/memory/stats.h"

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {
namespace TestUtil {

/**
 * Calls fn for a sampling of plausible stat names given a number of clusters.
 * This is intended for memory and performance benchmarking, where the syntax of
 * the names may be material to the measurements. Here we are deliberately not
 * claiming this is a complete stat set, which will change over time. Instead we
 * are aiming for consistency over time in order to create unit tests against
 * fixed memory budgets.
 *
 * @param num_clusters the number of clusters for which to generate stats.
 * @param fn the function to call with every stat name.
 */
void forEachSampleStat(int num_clusters, std::function<void(absl::string_view)> fn);

// Tracks memory consumption over a span of time. Test classes instantiate a
// MemoryTest object to start measuring heap memory, and call consumedBytes() to
// determine how many bytes have been consumed since the class was instantiated.
//
// That value should then be passed to EXPECT_MEMORY_EQ and EXPECT_MEMORY_LE,
// defined below, as the interpretation of this value can differ based on
// platform and compilation mode.
class MemoryTest {
public:
  // There are 3 cases:
  //   1. Memory usage API is available, and is built using with a canonical
  //      toolchain, enabling exact comparisons against an expected number of
  //      bytes consumed. The canonical environment is Envoy CI release builds.
  //   2. Memory usage API is available, but the current build may subtly differ
  //      in memory consumption from #1. We'd still like to track memory usage
  //      but it needs to be approximate.
  //   3. Memory usage API is not available. In this case, the code is executed
  //      but no testing occurs.
  enum class Mode {
    Disabled,    // No memory usage data available on platform.
    Canonical,   // Memory usage is available, and current platform is canonical.
    Approximate, // Memory usage is available, but variances form canonical expected.
  };

  MemoryTest() : memory_at_construction_(Memory::Stats::totalCurrentlyAllocated()) {}

  /**
   * @return the memory execution testability mode for the current compiler, architecture,
   *         and compile flags.
   */
  static Mode mode();

  size_t consumedBytes() const {
    // Note that this subtraction of two unsigned numbers will yield a very
    // large number if memory has actually shrunk since construction. In that
    // case, the EXPECT_MEMORY_EQ and EXPECT_MEMORY_LE macros will both report
    // failures, as desired, though the failure log may look confusing.
    //
    // Note also that tools like ubsan may report this as an unsigned integer
    // underflow, if run with -fsanitize=unsigned-integer-overflow, though
    // strictly speaking this is legal and well-defined for unsigned integers.
    return Memory::Stats::totalCurrentlyAllocated() - memory_at_construction_;
  }

private:
  const size_t memory_at_construction_;
};

// Compares the memory consumed against an exact expected value, but only on
// canonical platforms, or when the expected value is zero. Canonical platforms
// currently include only for 'release' tests in ci. On other platforms an info
// log is emitted, indicating that the test is being skipped.
#define EXPECT_MEMORY_EQ(consumed_bytes, expected_value)                                           \
  do {                                                                                             \
    if (expected_value == 0 ||                                                                     \
        Stats::TestUtil::MemoryTest::mode() == Stats::TestUtil::MemoryTest::Mode::Canonical) {     \
      EXPECT_EQ(consumed_bytes, expected_value);                                                   \
    } else {                                                                                       \
      ENVOY_LOG_MISC(info,                                                                         \
                     "Skipping exact memory test of actual={} versus expected={} "                 \
                     "bytes as platform is non-canonical",                                         \
                     consumed_bytes, expected_value);                                              \
    }                                                                                              \
  } while (false)

// Compares the memory consumed against an expected upper bound, but only
// on platforms where memory consumption can be measured via API. This is
// currently enabled only for builds with TCMALLOC. On other platforms, an info
// log is emitted, indicating that the test is being skipped.
#define EXPECT_MEMORY_LE(consumed_bytes, upper_bound)                                              \
  do {                                                                                             \
    if (Stats::TestUtil::MemoryTest::mode() != Stats::TestUtil::MemoryTest::Mode::Disabled) {      \
      EXPECT_LE(consumed_bytes, upper_bound);                                                      \
      EXPECT_GT(consumed_bytes, 0);                                                                \
    } else {                                                                                       \
      ENVOY_LOG_MISC(                                                                              \
          info, "Skipping upper-bound memory test against {} bytes as platform lacks tcmalloc",    \
          upper_bound);                                                                            \
    }                                                                                              \
  } while (false)

} // namespace TestUtil
} // namespace Stats
} // namespace Envoy
