// TODO(jmarantz): rename this file and class to heap_allocator.h.

#pragma once

#include <string>
#include <vector>

#include "envoy/stats/stat_data_allocator.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/symbol_table.h"

#include "common/common/assert.h"
#include "common/stats/metric_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

class HeapStatDataAllocator : public StatDataAllocator {
public:
  HeapStatDataAllocator(SymbolTable& symbol_table) : symbol_table_(symbol_table) {}
  ~HeapStatDataAllocator() override;

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
  friend class CounterImpl;
  friend class GaugeImpl;

  void freeCounter(StatName stat_name);
  void freeGauge(StatName stat_name);

  struct StorageHash {
    // See description for HeterogeneousStatNameHash::is_transparent.
    using is_transparent = void;

    size_t operator()(uint8_t* a) const { return StatName(a).hash(); }
    size_t operator()(StatName a) const { return a.hash(); }
  };

  struct StorageCompare {
    // See description for HeterogeneousStatNameHash::is_transparent.
    using is_transparent = void;

    size_t operator()(uint8_t* a, uint8_t* b) const { return StatName(a) == StatName(b); }
    size_t operator()(StatName a, uint8_t* b) const { return a == StatName(b); }
    size_t operator()(uint8_t* a, StatName b) const { return StatName(a) == b; }
  };

  template <class StatType>
  using StatNameMap =
      absl::flat_hash_map<uint8_t*, std::weak_ptr<StatType>, StorageHash, StorageCompare>;

  // An unordered map of StatNameStorage to shared pointers to metrics.
  // field in each object. This necessitates a custom comparator and hasher, which key off of the
  // StatNamePtr's own StatNamePtrHash and StatNamePtrCompare operators.
  StatNameMap<Counter> counters_ GUARDED_BY(mutex_);
  StatNameMap<Gauge> gauges_ GUARDED_BY(mutex_);

  SymbolTable& symbol_table_;

  // A mutex is needed here to protect both the stats_ object from both
  // alloc() and free() operations. Although alloc() operations are called under existing locking,
  // free() operations are made from the destructors of the individual stat objects, which are not
  // protected by locks.
  Thread::MutexBasicLockable mutex_;
};

} // namespace Stats
} // namespace Envoy
