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
#include "common/stats/symbol_table_impl.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Stats {

/**
 * This structure is an alternate backing store for both CounterImpl and GaugeImpl. It is designed
 * so that it can be allocated efficiently from the heap on demand.
 */
struct HeapStatData {
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
  SymbolTable::Storage symbol_storage_;
};

template <class Stat> class HeapStat : public Stat {
public:
  HeapStat(HeapStatData& data, StatDataAllocatorImpl<HeapStatData>& alloc,
           absl::string_view tag_extracted_name, const std::vector<Tag>& tags)
      : Stat(data, alloc, tag_extracted_name, tags) {}

  StatName statName() const override { return this->data_.statName(); }
};

// Partially implements a StatDataAllocator, leaving alloc & free for subclasses.
// We templatize on StatData rather than defining a virtual base StatData class
// for performance reasons; stat increment is on the hot path.
//
// The two production derivations cover using a fixed block of shared-memory for
// hot restart stat continuity, and heap allocation for more efficient RAM usage
// for when hot-restart is not required.
//
// Also note that RawStatData needs to live in a shared memory block, and it's
// possible, but not obvious, that a vptr would be usable across processes. In
// any case, RawStatData is allocated from a shared-memory block rather than via
// new, so the usual C++ compiler assistance for setting up vptrs will not be
// available. This could be resolved with placed new, or another nesting level.
class HeapStatDataAllocator : public StatDataAllocatorImpl<HeapStatData> {
public:
  HeapStatDataAllocator(SymbolTable& symbol_table) : StatDataAllocatorImpl(symbol_table) {}
  ~HeapStatDataAllocator() override;

  HeapStatData& alloc(StatName name);
  void free(HeapStatData& data) override;

  // StatDataAllocator
  bool requiresBoundedStatNameSize() const override { return false; }

  CounterSharedPtr makeCounter(StatName name, absl::string_view tag_extracted_name,
                               const std::vector<Tag>& tags) override {
    return std::make_shared<HeapStat<CounterImpl<HeapStatData>>>(alloc(name), *this,
                                                                 tag_extracted_name, tags);
  }

  GaugeSharedPtr makeGauge(StatName name, absl::string_view tag_extracted_name,
                           const std::vector<Tag>& tags) override {
    return std::make_shared<HeapStat<GaugeImpl<HeapStatData>>>(alloc(name), *this,
                                                               tag_extracted_name, tags);
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
