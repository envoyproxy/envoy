// TODO(jmarantz): rename this file and class to heap_allocator.h.

#pragma once

#include <string>
#include <vector>

#include "envoy/stats/stat_data_allocator.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/symbol_table.h"

#include "common/common/assert.h"
#include "common/stats/metric_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

class HeapStatDataAllocator : public StatDataAllocator {
public:
  HeapStatDataAllocator(SymbolTable& symbol_table) : symbol_table_(symbol_table) {}

  // StatDataAllocator
  CounterSharedPtr makeCounter(StatName name, absl::string_view tag_extracted_name,
                               const std::vector<Tag>& tags) override;
  GaugeSharedPtr makeGauge(StatName name, absl::string_view tag_extracted_name,
                           const std::vector<Tag>& tags, Gauge::ImportMode import_mode) override;

#ifndef ENVOY_CONFIG_COVERAGE
  void debugPrint();
#endif

  SymbolTable& symbolTable() override { return symbol_table_; }
  const SymbolTable& constSymbolTable() const override { return symbol_table_; }

private:
  template<class StatType>
  struct Hash {
    // See description for HeterogeneousStatNameHash::is_transparent.
    using is_transparent = void;

    size_t operator()(StatName a) const { return a.hash(); }
    size_t operator()(const std::shared_ptr<StatType>& a) const { return a->statName().hash(); }
  };

  template<class StatType>
  struct Compare {
    // See description for HeterogeneousStatNameHash::is_transparent.
    using is_transparent = void;

    using SharedPtr = std::shared_ptr<StatType>;

    size_t operator()(const SharedPtr& a, const SharedPtr& b) const {
      return a->statName() == b->statName();
    }
    size_t operator()(StatName a, const SharedPtr& b) const { return a == b->statName(); }
    size_t operator()(const SharedPtr& a, StatName b) const { return a->statName() == b; }
  };

  // An unordered set of HeapStatData pointers which keys off the key()
  // field in each object. This necessitates a custom comparator and hasher, which key off of the
  // StatNamePtr's own StatNamePtrHash and StatNamePtrCompare operators.
  template<class StatType> using StatSet =
      absl::flat_hash_set<std::shared_ptr<StatType>, Hash<StatType>, Compare<StatType>>;
  StatSet<Counter> counters_ GUARDED_BY(mutex_);
  StatSet<Gauge> gauges_ GUARDED_BY(mutex_);

  SymbolTable& symbol_table_;

  // A mutex is needed here to protect both the stats_ object from both
  // alloc() and free() operations. Although alloc() operations are called under existing locking,
  // free() operations are made from the destructors of the individual stat objects, which are not
  // protected by locks.
  Thread::MutexBasicLockable mutex_;
};

} // namespace Stats
} // namespace Envoy
