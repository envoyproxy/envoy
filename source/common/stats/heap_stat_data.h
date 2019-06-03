#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

#include "envoy/stats/stats.h"
#include "envoy/stats/symbol_table.h"

#include "common/common/hash.h"
#include "common/common/thread.h"
#include "common/common/thread_annotations.h"
#include "common/stats/metric_impl.h"
#include "common/stats/stat_data_allocator_impl.h"
#include "common/stats/stat_merger.h"
#include "common/stats/symbol_table_impl.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Stats {

/**
 * This structure is an alternate backing store for both CounterImpl and GaugeImpl. It is designed
 * so that it can be allocated efficiently from the heap on demand.
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

template <class Stat> class HeapStat : public Stat {
public:
  HeapStat(HeapStatData& data, StatDataAllocatorImpl<HeapStatData>& alloc,
           absl::string_view tag_extracted_name, const std::vector<Tag>& tags)
      : Stat(data, alloc, tag_extracted_name, tags) {}

  HeapStat(HeapStatData& data, StatDataAllocatorImpl<HeapStatData>& alloc,
           absl::string_view tag_extracted_name, const std::vector<Tag>& tags,
           Gauge::ImportMode import_mode)
      : Stat(data, alloc, tag_extracted_name, tags, import_mode) {}

  StatName statName() const override { return this->data_.statName(); }
};

class HeapStatDataAllocator : public StatDataAllocatorImpl<HeapStatData> {
public:
  HeapStatDataAllocator(SymbolTable& symbol_table) : StatDataAllocatorImpl(symbol_table) {}
  ~HeapStatDataAllocator() override;

  HeapStatData& alloc(StatName name);
  void free(HeapStatData& data) override;

  // StatDataAllocator
  CounterSharedPtr makeCounter(StatName name, absl::string_view tag_extracted_name,
                               const std::vector<Tag>& tags) override {
    return std::make_shared<HeapStat<CounterImpl<HeapStatData>>>(alloc(name), *this,
                                                                 tag_extracted_name, tags);
  }

  GaugeSharedPtr makeGauge(StatName name, absl::string_view tag_extracted_name,
                           const std::vector<Tag>& tags, Gauge::ImportMode import_mode) override {
    GaugeSharedPtr gauge = std::make_shared<HeapStat<GaugeImpl<HeapStatData>>>(
        alloc(name), *this, tag_extracted_name, tags, import_mode);

    // TODO(jmarantz): Remove this double-checking ASAP. This is left in only
    // for the transition while #7083 gets reviewed, while new code may be
    // concurrently added that updates the regex library. Once #7083 is merged
    // the regex table and this double-check can be deleted.
    switch (gauge->importMode()) {
    case Gauge::ImportMode::Accumulate:
      if (!StatMerger::shouldImportBasedOnRegex(gauge->name())) {
        std::cerr << "ImportMode conflict: regex says no, arg says yes: " << gauge->name()
                  << std::endl;
        new std::string; // Makes tests fail due to leak, but allows the process to keep running.
      }
      break;
    case Gauge::ImportMode::NeverImport:
      if (StatMerger::shouldImportBasedOnRegex(gauge->name())) {
        std::cerr << "ImportMode conflict: regex says yes, arg says no: " << gauge->name()
                  << std::endl;
        new std::string; // Makes tests fail due to leak, but allows the process to keep running.
      }
      break;
    case Gauge::ImportMode::Uninitialized:
      break;
    }
    return gauge;
  }

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

  // A mutex is needed here to protect both the stats_ object from both
  // alloc() and free() operations. Although alloc() operations are called under existing locking,
  // free() operations are made from the destructors of the individual stat objects, which are not
  // protected by locks.
  Thread::MutexBasicLockable mutex_;
};

} // namespace Stats
} // namespace Envoy
