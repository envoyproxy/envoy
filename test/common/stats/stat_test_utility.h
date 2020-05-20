#pragma once

#include "envoy/stats/store.h"

#include "common/common/logger.h"
#include "common/memory/stats.h"
#include "common/stats/isolated_store_impl.h"
#include "common/stats/symbol_table_creator.h"

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

// Helper class to use in lieu of an actual Stats::Store for doing lookups by
// name. The intent is to remove the deprecated Scope::counter(const
// std::string&) methods, and always use this class for accessing stats by
// name.
//
// This string-based lookup wrapper is needed because the underlying name
// representation, StatName, has multiple ways to represent the same string,
// depending on which name segments are symbolic (known at compile time), and
// which are dynamic (e.g. based on the request, e.g. request-headers, ssl
// cipher, grpc method, etc). While the production Store implementations
// use the StatName as a key, we must use strings in tests to avoid forcing
// the tests to construct the StatName using the same pattern of dynamic
// and symbol strings as production.
class TestStore : public IsolatedStoreImpl {
public:
  TestStore() = default;

  // Constructs a store using a symbol table, allowing for explicit sharing.
  explicit TestStore(SymbolTable& symbol_table) : IsolatedStoreImpl(symbol_table) {}

  Counter& counter(const std::string& name) { return counterFromString(name); }
  Gauge& gauge(const std::string& name, Gauge::ImportMode import_mode) {
    return gaugeFromString(name, import_mode);
  }
  Histogram& histogram(const std::string& name, Histogram::Unit unit) {
    return histogramFromString(name, unit);
  }
  TextReadout& textReadout(const std::string& name) { return textReadoutFromString(name); }

  // Override the Stats::Store methods for name-based lookup of stats, to use
  // and update the string-maps in this class. Note that IsolatedStoreImpl
  // does not support deletion of stats, so we only have to track additions
  // to keep the maps up-to-date.
  //
  // Stats::Scope
  Counter& counterFromString(const std::string& name) override;
  Gauge& gaugeFromString(const std::string& name, Gauge::ImportMode import_mode) override;
  Histogram& histogramFromString(const std::string& name, Histogram::Unit unit) override;
  Counter& counterFromStatNameWithTags(const StatName& name,
                                       StatNameTagVectorOptConstRef tags) override;
  Gauge& gaugeFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                   Gauge::ImportMode import_mode) override;
  Histogram& histogramFromStatNameWithTags(const StatName& name, StatNameTagVectorOptConstRef tags,
                                           Histogram::Unit unit) override;

  // New APIs available for tests.
  CounterOptConstRef findCounterByString(const std::string& name) const;
  GaugeOptConstRef findGaugeByString(const std::string& name) const;
  HistogramOptConstRef findHistogramByString(const std::string& name) const;

private:
  absl::flat_hash_map<std::string, Counter*> counter_map_;
  absl::flat_hash_map<std::string, Gauge*> gauge_map_;
  absl::flat_hash_map<std::string, Histogram*> histogram_map_;
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

class SymbolTableCreatorTestPeer {
public:
  ~SymbolTableCreatorTestPeer() { SymbolTableCreator::setUseFakeSymbolTables(save_use_fakes_); }

  void setUseFakeSymbolTables(bool use_fakes) {
    SymbolTableCreator::setUseFakeSymbolTables(use_fakes);
  }

private:
  const bool save_use_fakes_{SymbolTableCreator::useFakeSymbolTables()};
};

// Serializes a number into a uint8_t array, and check that it de-serializes to
// the same number. The serialized number is also returned, which can be
// checked in unit tests, but ignored in fuzz tests.
std::vector<uint8_t> serializeDeserializeNumber(uint64_t number);

// Serializes a string into a MemBlock and then decodes it.
void serializeDeserializeString(absl::string_view in);

} // namespace TestUtil
} // namespace Stats
} // namespace Envoy
