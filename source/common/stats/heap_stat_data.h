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
#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * This structure holds backing store for both CounterImpl and GaugeImpl,
 * counters and gauges allocated with the same name from different scopes
 * to share the same value.
 */
struct HeapStatData : public InlineStorage {
private:
  explicit HeapStatData(StatName stat_name) { stat_name.copyToStorage(symbol_storage_); }

public:
  static HeapStatData* alloc(StatName stat_name, SymbolTable& symbol_table);

  void free(SymbolTable& symbol_table);
  StatName statName() const { return StatName(symbol_storage_); }

  bool operator==(const HeapStatData& rhs) const { return statName() == rhs.statName(); }
  uint64_t hash() const { return statName().hash(); }

  std::atomic<uint64_t> value_{0};
  std::atomic<uint64_t> pending_increment_{0};
  std::atomic<uint16_t> flags_{0};
  std::atomic<uint16_t> ref_count_{1};
  SymbolTable::Storage symbol_storage_; // This is a 'using' nickname for uint8_t[].
};

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
  struct HeapStatHash {
    size_t operator()(const HeapStatData* a) const { return a->hash(); }
  };
  struct HeapStatCompare {
    bool operator()(const HeapStatData* a, const HeapStatData* b) const { return *a == *b; }
  };

  HeapStatData& alloc(StatName name);
  void free(HeapStatData& data);

  // An unordered set of HeapStatData pointers which keys off the key()
  // field in each object. This necessitates a custom comparator and hasher, which key off of the
  // StatNamePtr's own StatNamePtrHash and StatNamePtrCompare operators.
  using StatSet = absl::flat_hash_set<HeapStatData*, HeapStatHash, HeapStatCompare>;
  StatSet stats_ GUARDED_BY(mutex_);

  friend class CounterImpl;
  friend class GaugeImpl;

  /*
  void freeCounter(StatName stat_name);
  void freeGauge(StatName stat_name);

  template<class StatType> struct StatHash {
    // See description for HeterogeneousStatNameHash::is_transparent.
    using is_transparent = void;
    using WeakStat = std::weak_ptr<StatType>;

    size_t operator()(const WeakStat& a) const { return a.lock()->statName().hash(); }
    size_t operator()(StatName a) const { return a.hash(); }
  };

  template<class StatType> struct StatCompare {
    // See description for HeterogeneousStatNameHash::is_transparent.
    using is_transparent = void;
    using WeakStat = std::weak_ptr<StatType>;

    size_t operator()(const WeakStat& a, const WeakStat& b) const {
      return a.lock()->statName() == b.lock()->statName();
    }
    size_t operator()(StatName a, const WeakStat& b) const { return a == b.lock()->statName(); }
    size_t operator()(const WeakStat& a, StatName b) const { return a.lock()->statName() == b; }
  };

  template <class StatType> using StatSet = absl::flat_hash_set<
      std::weak_ptr<StatType>, StatHash<StatType>, StatCompare<StatType>>;

  // An unordered map of StatNameStorage to shared pointers to metrics.
  // field in each object. This necessitates a custom comparator and hasher, which key off of the
  // StatNamePtr's own StatNamePtrHash and StatNamePtrCompare operators.
  StatSet<Counter> counters_ GUARDED_BY(mutex_);
  StatSet<Gauge> gauges_ GUARDED_BY(mutex_);
  */

  SymbolTable& symbol_table_;

  // A mutex is needed here to protect both the stats_ object from both
  // alloc() and free() operations. Although alloc() operations are called under existing locking,
  // free() operations are made from the destructors of the individual stat objects, which are not
  // protected by locks.
  Thread::MutexBasicLockable mutex_;
};

} // namespace Stats
} // namespace Envoy
