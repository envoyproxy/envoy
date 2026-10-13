#pragma once

#include <vector>

#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"

#include "source/common/common/thread.h"
#include "source/common/common/thread_synchronizer.h"
#include "source/common/stats/metric_impl.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Stats {

/**
 * Helper class for Store to manage memory for statistics.
 */
class Allocator {
public:
  static const char DecrementToZeroSyncPoint[];

  Allocator(SymbolTable& symbol_table) : symbol_table_(symbol_table) {}
  virtual ~Allocator();

  /**
   * @param name the full name of the stat.
   * @param tag_extracted_name the name of the stat with tag-values stripped out.
   * @param tags the tag values.
   * @return CounterSharedPtr a counter.
   */
  CounterSharedPtr makeCounter(StatName name, StatName tag_extracted_name,
                               StatNameTagSpan stat_name_tags);

  /**
   * @param name the full name of the stat.
   * @param tag_extracted_name the name of the stat with tag-values stripped out.
   * @param stat_name_tags the tag values.
   * @return GaugeSharedPtr a gauge.
   */
  GaugeSharedPtr makeGauge(StatName name, StatName tag_extracted_name,
                           StatNameTagSpan stat_name_tags, Gauge::ImportMode import_mode);

  /**
   * @param name the full name of the stat.
   * @param tag_extracted_name the name of the stat with tag-values stripped out.
   * @param tags the tag values.
   * @return TextReadoutSharedPtr a text readout.
   */
  TextReadoutSharedPtr makeTextReadout(StatName name, StatName tag_extracted_name,
                                       StatNameTagSpan stat_name_tags);
  SymbolTable& symbolTable() { return symbol_table_; }
  const SymbolTable& constSymbolTable() const { return symbol_table_; }

  /**
   * Iterate over all stats. Note, that implementations can potentially hold on to a mutex that
   * will deadlock if the passed in functors try to create or delete a stat.
   * @param f_size functor that is provided the current number of all stats. Note that this is
   * called only once, prior to any calls to f_stat.
   * @param f_stat functor that is provided one stat at a time from the stats container.
   */
  void forEachCounter(SizeFn, StatFn<Counter>) const;
  void forEachGauge(SizeFn, StatFn<Gauge>) const;
  void forEachTextReadout(SizeFn, StatFn<TextReadout>) const;

  /**
   * Iterate over all stats that need to be flushed to sinks. Note, that implementations can
   * potentially hold on to a mutex that will deadlock if the passed in functors try to create
   * or delete a stat.
   * @param f_size functor that is provided the number of all stats that will be flushed to sinks.
   * Note that this is called only once, prior to any calls to f_stat.
   * @param f_stat functor that is provided one stat that will be flushed to sinks, at a time.
   */
  void forEachSinkedCounter(SizeFn f_size, StatFn<Counter> f_stat) const;
  void forEachSinkedGauge(SizeFn f_size, StatFn<Gauge> f_stat) const;
  void forEachSinkedTextReadout(SizeFn f_size, StatFn<TextReadout> f_stat) const;

  /**
   * Set the predicates to filter stats for sink.
   */
  void setSinkPredicates(std::unique_ptr<SinkPredicates>&& sink_predicates);
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

  /**
   * Mark rejected stats as deleted by moving them to a different vector, so they don't show up
   * when iterating over stats, but prevent crashes when trying to access references to them.
   * Note that allocating a stat with the same name after calling this will
   * return a new stat. Hence callers should seek to avoid this situation, as is
   * done in ThreadLocalStore.
   */
  void markCounterForDeletion(const CounterSharedPtr& counter);
  void markGaugeForDeletion(const GaugeSharedPtr& gauge);
  void markTextReadoutForDeletion(const TextReadoutSharedPtr& text_readout);

protected:
  virtual Counter* makeCounterInternal(StatName name, StatName tag_extracted_name,
                                       StatNameTagSpan stat_name_tags);

private:
  template <class BaseClass> friend class StatsSharedImpl;
  friend class CounterImpl;
  friend class GaugeImpl;
  friend class TextReadoutImpl;

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
