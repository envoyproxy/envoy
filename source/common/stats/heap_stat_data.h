// TODO(jmarantz): rename this file and class to heap_allocator.h.

#pragma once

#include <vector>

#include "envoy/stats/stat_data_allocator.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/symbol_table.h"

#include "common/stats/metric_impl.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

/**
 * Holds backing store for both CounterImpl and GaugeImpl. This provides a level
 * of indirection needed to enable stats created with the same name from
 * different scopes to share the same value.
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

  HeapStatData& alloc(StatName name);
  void free(HeapStatData& data);

  // StatDataAllocator
  CounterSharedPtr makeCounter(StatName name, absl::string_view tag_extracted_name,
                               const std::vector<Tag>& tags) override;
  GaugeSharedPtr makeGauge(StatName name, absl::string_view tag_extracted_name,
                           const std::vector<Tag>& tags, Gauge::ImportMode import_mode) override;
  SymbolTable& symbolTable() override { return symbol_table_; }
  const SymbolTable& constSymbolTable() const override { return symbol_table_; }

#ifndef ENVOY_CONFIG_COVERAGE
  void debugPrint();
#endif

private:
  struct HeapStatHash {
    size_t operator()(const HeapStatData* a) const { return a->hash(); }
  };
  struct HeapStatCompare {
    bool operator()(const HeapStatData* a, const HeapStatData* b) const { return *a == *b; }
  };

  // An unordered set of HeapStatData pointers which keys off the key()
  // field in each object. This necessitates a custom comparator and hasher, which key off of the
  // StatNamePtr's own StatNamePtrHash and StatNamePtrCompare operators.
  using StatSet = absl::flat_hash_set<HeapStatData*, HeapStatHash, HeapStatCompare>;
  StatSet stats_ GUARDED_BY(mutex_);

  SymbolTable& symbol_table_;

  // A mutex is needed here to protect both the stats_ object from both
  // alloc() and free() operations. Although alloc() operations are called under existing locking,
  // free() operations are made from the destructors of the individual stat objects, which are not
  // protected by locks.
  Thread::MutexBasicLockable mutex_;
};

} // namespace Stats
} // namespace Envoy
