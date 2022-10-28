#pragma once

#include <vector>

#include "envoy/common/optref.h"
#include "envoy/stats/allocator.h"
#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"

#include "source/common/common/thread_synchronizer.h"
#include "source/common/stats/metric_impl.h"

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
  TextReadoutSharedPtr makeTextReadout(StatName name, StatName tag_extracted_name,
                                       const StatNameTagVector& stat_name_tags) override;
  SymbolTable& symbolTable() override { return symbol_table_; }
  const SymbolTable& constSymbolTable() const override { return symbol_table_; }

  void forEachCounter(SizeFn, StatFn<Counter>) const override;

  void forEachGauge(SizeFn, StatFn<Gauge>) const override;

  void forEachTextReadout(SizeFn, StatFn<TextReadout>) const override;

  void forEachSinkedCounter(SizeFn f_size, StatFn<Counter> f_stat) const override;
  void forEachSinkedGauge(SizeFn f_size, StatFn<Gauge> f_stat) const override;
  void forEachSinkedTextReadout(SizeFn f_size, StatFn<TextReadout> f_stat) const override;

  void setSinkPredicates(std::unique_ptr<SinkPredicates>&& sink_predicates) override;
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

  void markCounterForDeletion(const CounterSharedPtr& counter) override;
  void markGaugeForDeletion(const GaugeSharedPtr& gauge) override;
  void markTextReadoutForDeletion(const TextReadoutSharedPtr& text_readout) override;

protected:
  virtual Counter* makeCounterInternal(StatName name, StatName tag_extracted_name,
                                       const StatNameTagVector& stat_name_tags);

private:
  template <class BaseClass> friend class StatsSharedImpl;
  friend class CounterImpl;
  friend class GaugeImpl;
  friend class TextReadoutImpl;
  friend class NotifyingAllocatorImpl;

  // A mutex is needed here to protect both the stats_ object from both
  // alloc() and free() operations. Although alloc() operations are called under existing locking,
  // free() operations are made from the destructors of the individual stat objects, which are not
  // protected by locks.
  mutable Thread::MutexBasicLockable mutex_;

  StatSet<Counter> counters_ ABSL_GUARDED_BY(mutex_);
  StatSet<Gauge> gauges_ ABSL_GUARDED_BY(mutex_);
  StatSet<TextReadout> text_readouts_ ABSL_GUARDED_BY(mutex_);

  template <typename StatType> using StatPointerSet = absl::flat_hash_set<StatType*>;
  // Stat pointers that participate in the flush to sink process.
  StatPointerSet<Counter> sinked_counters_ ABSL_GUARDED_BY(mutex_);
  StatPointerSet<Gauge> sinked_gauges_ ABSL_GUARDED_BY(mutex_);
  StatPointerSet<TextReadout> sinked_text_readouts_ ABSL_GUARDED_BY(mutex_);

  // Predicates used to filter stats to be flushed.
  std::unique_ptr<SinkPredicates> sink_predicates_;
  SymbolTable& symbol_table_;

  Thread::ThreadSynchronizer sync_;

  // Retain storage for deleted stats; these are no longer in maps because
  // the matcher-pattern was established after they were created. Since the
  // stats are held by reference in code that expects them to be there, we
  // can't actually delete the stats.
  //
  // It seems like it would be better to have each client that expects a stat
  // to exist to hold it as (e.g.) a CounterSharedPtr rather than a Counter&
  // but that would be fairly complex to change.
  std::vector<CounterSharedPtr> deleted_counters_ ABSL_GUARDED_BY(mutex_);
  std::vector<GaugeSharedPtr> deleted_gauges_ ABSL_GUARDED_BY(mutex_);
  std::vector<TextReadoutSharedPtr> deleted_text_readouts_ ABSL_GUARDED_BY(mutex_);
};

} // namespace Stats
} // namespace Envoy
