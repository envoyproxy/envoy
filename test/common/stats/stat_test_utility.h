#pragma once

#include "envoy/stats/store.h"

#include "source/common/common/logger.h"
#include "source/common/stats/isolated_store_impl.h"

#include "test/common/memory/memory_test_utility.h"
#include "test/test_common/global.h"

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

// Helper methods to facilitate using testing::ElementsAre with bucket vectors.
bool operator==(const ParentHistogram::Bucket& a, const ParentHistogram::Bucket& b);
std::ostream& operator<<(std::ostream& out, const ParentHistogram::Bucket& bucket);

namespace TestUtil {

class TestSymbolTableHelper {
public:
  SymbolTable& symbolTable() { return symbol_table_; }
  const SymbolTable& constSymbolTable() const { return symbol_table_; }

private:
  SymbolTableImpl symbol_table_;
};

// Symbol table wrapper that instantiates a shared, reference-counted, global
// symbol table. This is needed by the mocking infrastructure, as Envoy mocks
// are constructed without any context, but StatNames that are symbolized from
// one mock may need to be entered into stat storage in another one. Thus they
// must be connected by global state.
class TestSymbolTable {
public:
  SymbolTable& operator*() { return global_.get().symbolTable(); }
  const SymbolTable& operator*() const { return global_.get().constSymbolTable(); }
  SymbolTable* operator->() { return &global_.get().symbolTable(); }
  const SymbolTable* operator->() const { return &global_.get().constSymbolTable(); }
  Envoy::Test::Global<TestSymbolTableHelper> global_;
};

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
void forEachSampleStat(int num_clusters, bool include_other_stats,
                       std::function<void(absl::string_view)> fn);

class SymbolTableProvider {
public:
  TestSymbolTable global_symbol_table_;
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
class TestStore : public SymbolTableProvider, public IsolatedStoreImpl {
public:
  TestStore();

  // Constructs a store using a symbol table, allowing for explicit sharing.
  explicit TestStore(SymbolTable& symbol_table);

  Counter& counter(const std::string& name) { return rootScope()->counterFromString(name); }
  Gauge& gauge(const std::string& name, Gauge::ImportMode import_mode) {
    return rootScope()->gaugeFromString(name, import_mode);
  }
  Histogram& histogram(const std::string& name, Histogram::Unit unit) {
    return rootScope()->histogramFromString(name, unit);
  }
  TextReadout& textReadout(const std::string& name) {
    return rootScope()->textReadoutFromString(name);
  }
  void deliverHistogramToSinks(const Histogram& histogram, uint64_t value) override {
    histogram_values_map_[histogram.name()].push_back(value);
  }

  // New APIs available for tests.
  CounterOptConstRef findCounterByString(const std::string& name) const;
  GaugeOptConstRef findGaugeByString(const std::string& name) const;
  HistogramOptConstRef findHistogramByString(const std::string& name) const;
  std::vector<uint64_t> histogramValues(const std::string& name, bool clear);
  // Returns whether the given histogram has recorded any value since it was
  // created.
  bool histogramRecordedValues(const std::string& name) const;
  const TagVector& fixedTags() override { return fixed_tags_; }

  TagVector fixed_tags_;

protected:
  ScopeSharedPtr makeScope(StatName name) override;

private:
  friend class TestScope;

  // The Store keeps a flat map of all the counters in all scopes.
  absl::flat_hash_map<std::string, Counter*> counter_map_;
  absl::flat_hash_map<std::string, Gauge*> gauge_map_;
  absl::flat_hash_map<std::string, Histogram*> histogram_map_;
  absl::flat_hash_map<std::string, std::vector<uint64_t>> histogram_values_map_;
};

class TestScope : public IsolatedScopeImpl {
public:
  TestScope(const std::string& prefix, TestStore& store);
  TestScope(StatName prefix, TestStore& store);

  // Override the Stats::Store methods for name-based lookup of stats, to use
  // and update the string-maps in this class. Note that IsolatedStoreImpl
  // does not support deletion of stats, so we only have to track additions
  // to keep the maps up-to-date.
  //
  // Stats::Scope
  Counter& counterFromString(const std::string& name) override;
  Gauge& gaugeFromString(const std::string& name, Gauge::ImportMode import_mode) override;
  Histogram& histogramFromString(const std::string& name, Histogram::Unit unit) override;
  Counter& counterFromStatNameWithTags(const StatName& stat_name,
                                       StatNameTagVectorOptConstRef tags) override;
  Gauge& gaugeFromStatNameWithTags(const StatName& stat_name, StatNameTagVectorOptConstRef tags,
                                   Gauge::ImportMode import_mode) override;
  Histogram& histogramFromStatNameWithTags(const StatName& stat_name,
                                           StatNameTagVectorOptConstRef tags,
                                           Histogram::Unit unit) override;
  TestStore& store() override { return store_; }
  const TestStore& constStore() const override { return store_; }

private:
  std::string statNameWithTags(const StatName& stat_name, StatNameTagVectorOptConstRef tags);
  static std::string addDot(const std::string& prefix) {
    if (prefix.empty() || prefix[prefix.size() - 1] == '.') {
      return prefix;
    }
    return prefix + ".";
  }

  void verifyConsistency(StatName ref_stat_name, StatName stat_name,
                         StatNameTagVectorOptConstRef tags);

  TestStore& store_;
  const std::string prefix_str_;
};

// Serializes a number into a uint8_t array, and check that it de-serializes to
// the same number. The serialized number is also returned, which can be
// checked in unit tests, but ignored in fuzz tests.
std::vector<uint8_t> serializeDeserializeNumber(uint64_t number);

// Serializes a string into a MemBlock and then decodes it.
void serializeDeserializeString(absl::string_view in);

class TestSinkPredicates : public SinkPredicates {
public:
  ~TestSinkPredicates() override = default;

  bool has(StatName name) { return sinked_stat_names_.find(name) != sinked_stat_names_.end(); }

  // Note: The backing store for the StatName needs to live longer than the
  // TestSinkPredicates object.
  void add(StatName name) { sinked_stat_names_.insert(name); }

  // SinkPredicates
  bool includeCounter(const Counter& counter) override {
    return sinked_stat_names_.find(counter.statName()) != sinked_stat_names_.end();
  }
  bool includeGauge(const Gauge& gauge) override {
    return sinked_stat_names_.find(gauge.statName()) != sinked_stat_names_.end();
  }
  bool includeTextReadout(const TextReadout& text_readout) override {
    return sinked_stat_names_.find(text_readout.statName()) != sinked_stat_names_.end();
  }
  bool includeHistogram(const Histogram& histogram) override {
    return sinked_stat_names_.find(histogram.statName()) != sinked_stat_names_.end();
  }

private:
  StatNameHashSet sinked_stat_names_;
};

} // namespace TestUtil
} // namespace Stats
} // namespace Envoy
