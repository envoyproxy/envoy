#pragma once

#include <vector>

#include "envoy/stats/allocator.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/symbol_table.h"

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

  void forEachSinkedCounter(std::function<void(std::size_t)>,
                            std::function<void(Stats::Counter&)>) override;

  void forEachSinkedGauge(std::function<void(std::size_t)>,
                          std::function<void(Stats::Gauge&)>) override;

  void forEachSinkedTextReadout(std::function<void(std::size_t)>,
                                std::function<void(Stats::TextReadout&)>) override;

  void setCounterSinkFilter(std::function<bool(const Stats::Counter&)> filter) override;

  void setGaugeSinkFilter(std::function<bool(const Stats::Gauge&)> filter) override;

  void setTextReadoutSinkFilter(std::function<bool(const Stats::TextReadout&)> filter) override;

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

protected:
  virtual Counter* makeCounterInternal(StatName name, StatName tag_extracted_name,
                                       const StatNameTagVector& stat_name_tags);

private:
  template <class BaseClass> friend class StatsSharedImpl;
  friend class CounterImpl;
  friend class GaugeImpl;
  friend class TextReadoutImpl;
  friend class NotifyingAllocatorImpl;

  // We don't need to check StatName to compare sinked stats.
  template <typename StatType>
  using SinkedStatsSet = absl::flat_hash_set<StatType*, absl::Hash<StatType*>>;

  void removeCounterFromSetLockHeld(Counter* counter) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void removeGaugeFromSetLockHeld(Gauge* gauge) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void removeTextReadoutFromSetLockHeld(Counter* counter) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // Helper method to iterate over sinked stats based on whether or not a stats sink function was
  // provided.
  template <typename StatType>
  void forEachSinkedStat(std::function<void(std::size_t)>& f_size,
                         std::function<void(StatType&)>& f_stat,
                         std::function<bool(const StatType&)>& sink_filter,
                         StatSet<StatType>& stats, SinkedStatsSet<StatType>& sinked_stats);

  StatSet<Counter> counters_ ABSL_GUARDED_BY(mutex_);
  StatSet<Gauge> gauges_ ABSL_GUARDED_BY(mutex_);
  StatSet<TextReadout> text_readouts_ ABSL_GUARDED_BY(mutex_);

  SinkedStatsSet<Counter> sinked_counters_ ABSL_GUARDED_BY(mutex_);
  SinkedStatsSet<Gauge> sinked_gauges_ ABSL_GUARDED_BY(mutex_);
  SinkedStatsSet<TextReadout> sinked_text_readouts_ ABSL_GUARDED_BY(mutex_);

  std::function<bool(const Stats::Counter&)> counter_sink_filter_;
  std::function<bool(const Stats::Gauge&)> gauge_sink_filter_;
  std::function<bool(const Stats::TextReadout&)> text_readout_sink_filter_;

  SymbolTable& symbol_table_;

  // A mutex is needed here to protect both the stats_ object from both
  // alloc() and free() operations. Although alloc() operations are called under existing locking,
  // free() operations are made from the destructors of the individual stat objects, which are not
  // protected by locks.
  Thread::MutexBasicLockable mutex_;

  Thread::ThreadSynchronizer sync_;
};

template <typename StatType>
void AllocatorImpl::forEachSinkedStat(std::function<void(std::size_t)>& f_size,
                                      std::function<void(StatType&)>& f_stat,
                                      std::function<bool(const StatType&)>& sink_filter,
                                      StatSet<StatType>& stats,
                                      SinkedStatsSet<StatType>& sinked_stats) {
  ASSERT(!mutex_.tryLock());
  // Check if a sink filter method was provided.
  if (sink_filter != nullptr) {
    f_size(sinked_stats.size());
    for (auto& stat : sinked_stats) {
      f_stat(*stat);
    }
  } else {
    // Iterate over all stats if a filter method was not provided.
    f_size(stats.size());
    for (auto& stat : stats) {
      f_stat(*stat);
    }
  }
}

} // namespace Stats
} // namespace Envoy
