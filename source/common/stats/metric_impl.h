#pragma once

#include <string>
#include <vector>

#include "envoy/stats/allocator.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/tag.h"

#include "source/common/common/assert.h"
#include "source/common/stats/symbol_table.h"

namespace Envoy {
namespace Stats {

/**
 * Helper class for implementing Metrics. This does not participate in any
 * inheritance chains, but can be instantiated by classes that do. It just
 * implements the mechanics of representing the name, tag-extracted-name,
 * and all tags as a StatNameList.
 */
class MetricHelper {
public:
  MetricHelper(StatName name, StatName tag_extracted_name, const StatNameTagVector& stat_name_tags,
               SymbolTable& symbol_table);
  ~MetricHelper();

  StatName statName() const;
  std::string name(const SymbolTable& symbol_table) const;
  TagVector tags(const SymbolTable& symbol_table) const;
  StatName tagExtractedStatName() const;
  void iterateTagStatNames(const Metric::TagStatNameIterFn& fn) const;
  void clear(SymbolTable& symbol_table) { stat_names_.clear(symbol_table); }

  // Hasher for metrics.
  struct Hash {
    using is_transparent = void; // NOLINT(readability-identifier-naming)
    size_t operator()(const Metric* a) const { return a->statName().hash(); }
    size_t operator()(StatName a) const { return a.hash(); }
  };

  // Comparator for metrics.
  struct Compare {
    using is_transparent = void; // NOLINT(readability-identifier-naming)
    bool operator()(const Metric* a, const Metric* b) const {
      return a->statName() == b->statName();
    }
    bool operator()(const Metric* a, StatName b) const { return a->statName() == b; }
  };

private:
  StatNameList stat_names_;
};

// An unordered set of stat pointers. which keys off Metric::statName().
// This necessitates a custom comparator and hasher, using the StatNamePtr's
// own StatNamePtrHash and StatNamePtrCompare operators.
//
// This is used by AllocatorImpl for counters, gauges, and text-readouts, and
// is also used by thread_local_store.h for histograms.
template <class StatType>
using StatSet = absl::flat_hash_set<StatType*, MetricHelper::Hash, MetricHelper::Compare>;

/**
 * Partial implementation of the Metric interface on behalf of Counters, Gauges,
 * and Histograms. It leaves symbolTable() unimplemented so that implementations
 * of stats managed by an allocator, specifically Counters and Gauges, can keep
 * a reference to the allocator instead, and derive the symbolTable() from that.
 *
 * We templatize on the base class (Counter, Gauge, or Histogram), rather than
 * using multiple virtual inheritance, as this avoids the overhead of an extra
 * vptr per instance. This is important for stats because there can be many
 * stats in systems with large numbers of clusters and hosts, and a few 8-byte
 * pointers per-stat here and there can add up to significant amounts of memory.
 *
 * Note the delegation of the implementation to a helper class, which is neither
 * templatized nor virtual. This avoids having the compiler elaborate complete
 * copies of the underlying implementation for each base class during template
 * expansion.
 */
template <class BaseClass> class MetricImpl : public BaseClass {
public:
  MetricImpl(StatName name, StatName tag_extracted_name, const StatNameTagVector& stat_name_tags,
             SymbolTable& symbol_table)
      : helper_(name, tag_extracted_name, stat_name_tags, symbol_table) {}

  // Empty construction of a MetricImpl; used for null stats.
  explicit MetricImpl(SymbolTable& symbol_table)
      : MetricImpl(StatName(), StatName(), StatNameTagVector(), symbol_table) {}

  TagVector tags() const override { return helper_.tags(constSymbolTable()); }
  StatName statName() const override { return helper_.statName(); }
  StatName tagExtractedStatName() const override { return helper_.tagExtractedStatName(); }
  void iterateTagStatNames(const Metric::TagStatNameIterFn& fn) const override {
    helper_.iterateTagStatNames(fn);
  }

  const SymbolTable& constSymbolTable() const override {
    // Cast our 'this', which is of type `const MetricImpl*` to a non-const
    // pointer, so we can use it to call the subclass implementation of
    // symbolTable(). That will be returned as a non-const SymbolTable&,
    // which will become const on return.
    //
    // This pattern is used to share a single non-trivial implementation to
    // provide const and non-const variants of a method.
    return const_cast<MetricImpl*>(this)->symbolTable();
  }
  std::string name() const override { return constSymbolTable().toString(this->statName()); }
  std::string tagExtractedName() const override {
    return constSymbolTable().toString(this->tagExtractedStatName());
  }

protected:
  void clear(SymbolTable& symbol_table) { helper_.clear(symbol_table); }

private:
  MetricHelper helper_;
};

} // namespace Stats
} // namespace Envoy
