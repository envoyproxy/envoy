#pragma once

#include "source/common/memory/stats.h"

namespace Envoy {
namespace Memory {
namespace TestUtil {

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
    if (expected_value == 0 || ::Envoy::Memory::TestUtil::MemoryTest::mode() ==                    \
                                   ::Envoy::Memory::TestUtil::MemoryTest::Mode::Canonical) {       \
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
    if (::Envoy::Memory::TestUtil::MemoryTest::mode() !=                                           \
        ::Envoy::Memory::TestUtil::MemoryTest::Mode::Disabled) {                                   \
      EXPECT_LE(consumed_bytes, upper_bound);                                                      \
      if (upper_bound != 0) {                                                                      \
        EXPECT_GT(consumed_bytes, 0);                                                              \
      }                                                                                            \
    } else {                                                                                       \
      ENVOY_LOG_MISC(                                                                              \
          info, "Skipping upper-bound memory test against {} bytes as platform lacks tcmalloc",    \
          upper_bound);                                                                            \
    }                                                                                              \
  } while (false)

} // namespace TestUtil
} // namespace Memory
} // namespace Envoy
