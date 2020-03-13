#pragma once

#include <vector>

#include "envoy/stats/allocator.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/symbol_table.h"

#include "common/common/thread_synchronizer.h"
#include "common/stats/metric_impl.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

class AllocatorImpl : public Allocator {
public:
  static const char DecrementToZeroSyncPoint[];

  AllocatorImpl(SymbolTable& symbol_table) : symbol_table_(symbol_table) {}
  ~AllocatorImpl() override;

  // Allocator
  CounterSharedPtr makeCounter(StatName name, StatName tag_extracted_name,
                               const StatNameTagVector& stat_name_tags) override;
  GaugeSharedPtr makeGauge(StatName name, StatName tag_extracted_name,
                           const StatNameTagVector& stat_name_tags,
                           Gauge::ImportMode import_mode) override;
  SymbolTable& symbolTable() override { return symbol_table_; }
  const SymbolTable& constSymbolTable() const override { return symbol_table_; }

#ifndef ENVOY_CONFIG_COVERAGE
  void debugPrint();
#endif

  /**
   * @return a thread synchronizer object used for reproducing a race-condition in tests.
   */
  Thread::ThreadSynchronizer& sync() { return sync_; }

  /**
   * @return whether the allocator's mutex is locked, exposed for testing purposes.
   */
  bool isMutexLockedForTest();

private:
  template <class BaseClass> friend class StatsSharedImpl;
  friend class CounterImpl;
  friend class GaugeImpl;

  struct HeapStatHash {
    using is_transparent = void; // NOLINT(readability-identifier-naming)
    size_t operator()(const Metric* a) const { return a->statName().hash(); }
    size_t operator()(StatName a) const { return a.hash(); }
  };

  struct HeapStatCompare {
    using is_transparent = void; // NOLINT(readability-identifier-naming)
    bool operator()(const Metric* a, const Metric* b) const {
      return a->statName() == b->statName();
    }
    bool operator()(const Metric* a, StatName b) const { return a->statName() == b; }
  };

  void removeCounterFromSetLockHeld(Counter* counter) EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void removeGaugeFromSetLockHeld(Gauge* gauge) EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // An unordered set of HeapStatData pointers which keys off the key()
  // field in each object. This necessitates a custom comparator and hasher, which key off of the
  // StatNamePtr's own StatNamePtrHash and StatNamePtrCompare operators.
  template <class StatType>
  using StatSet = absl::flat_hash_set<StatType*, HeapStatHash, HeapStatCompare>;
  StatSet<Counter> counters_ GUARDED_BY(mutex_);
  StatSet<Gauge> gauges_ GUARDED_BY(mutex_);

  SymbolTable& symbol_table_;

  // A mutex is needed here to protect both the stats_ object from both
  // alloc() and free() operations. Although alloc() operations are called under existing locking,
  // free() operations are made from the destructors of the individual stat objects, which are not
  // protected by locks.
  Thread::MutexBasicLockable mutex_;

  Thread::ThreadSynchronizer sync_;
};

} // namespace Stats
} // namespace Envoy
