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

class CounterImpl;

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
  virtual CounterImpl* makeCounterImpl(StatName name, StatName tag_extracted_name,
                                       const StatNameTagVector& stat_name_tags);

private:
  template <class BaseClass> friend class StatsSharedImpl;
  friend class CounterImpl;
  friend class GaugeImpl;
  friend class TextReadoutImpl;
  friend class NotifyingAllocatorImpl;

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
  void removeTextReadoutFromSetLockHeld(Counter* counter) EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  // An unordered set of HeapStatData pointers which keys off the key()
  // field in each object. This necessitates a custom comparator and hasher, which key off of the
  // StatNamePtr's own StatNamePtrHash and StatNamePtrCompare operators.
  template <class StatType>
  using StatSet = absl::flat_hash_set<StatType*, HeapStatHash, HeapStatCompare>;
  StatSet<Counter> counters_ GUARDED_BY(mutex_);
  StatSet<Gauge> gauges_ GUARDED_BY(mutex_);
  StatSet<TextReadout> text_readouts_ GUARDED_BY(mutex_);

  SymbolTable& symbol_table_;

  // A mutex is needed here to protect both the stats_ object from both
  // alloc() and free() operations. Although alloc() operations are called under existing locking,
  // free() operations are made from the destructors of the individual stat objects, which are not
  // protected by locks.
  Thread::MutexBasicLockable mutex_;

  Thread::ThreadSynchronizer sync_;
};

// Counter, Gauge and TextReadout inherit from RefcountInterface and
// Metric. MetricImpl takes care of most of the Metric API, but we need to cover
// symbolTable() here, which we don't store directly, but get it via the alloc,
// which we need in order to clean up the counter and gauge maps in that class
// when they are destroyed.
//
// We implement the RefcountInterface API, using 16 bits that would otherwise be
// wasted in the alignment padding next to flags_.
template <class BaseClass> class StatsSharedImpl : public MetricImpl<BaseClass> {
public:
  StatsSharedImpl(StatName name, AllocatorImpl& alloc, StatName tag_extracted_name,
                  const StatNameTagVector& stat_name_tags)
      : MetricImpl<BaseClass>(name, tag_extracted_name, stat_name_tags, alloc.symbolTable()),
        alloc_(alloc) {}

  ~StatsSharedImpl() override {
    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    this->clear(symbolTable());
  }

  // Metric
  SymbolTable& symbolTable() override { return alloc_.symbolTable(); }
  bool used() const override { return flags_ & Metric::Flags::Used; }

  // RefcountInterface
  void incRefCount() override { ++ref_count_; }
  bool decRefCount() override {
    // We must, unfortunately, hold the allocator's lock when decrementing the
    // refcount. Otherwise another thread may simultaneously try to allocate the
    // same name'd stat after we decrement it, and we'll wind up with a
    // dtor/update race. To avoid this we must hold the lock until the stat is
    // removed from the map.
    //
    // It might be worth thinking about a race-free way to decrement ref-counts
    // without a lock, for the case where ref_count > 2, and we don't need to
    // destruct anything. But it seems preferable at to be conservative here,
    // as stats will only go out of scope when a scope is destructed (during
    // xDS) or during admin stats operations.
    Thread::LockGuard lock(alloc_.mutex_);
    ASSERT(ref_count_ >= 1);
    if (--ref_count_ == 0) {
      alloc_.sync().syncPoint(AllocatorImpl::DecrementToZeroSyncPoint);
      removeFromSetLockHeld();
      return true;
    }
    return false;
  }
  uint32_t use_count() const override { return ref_count_; }

  /**
   * We must atomically remove the counter/gauges from the allocator's sets when
   * our ref-count decrement hits zero. The counters and gauges are held in
   * distinct sets so we virtualize this removal helper.
   */
  virtual void removeFromSetLockHeld() EXCLUSIVE_LOCKS_REQUIRED(alloc_.mutex_) PURE;

protected:
  AllocatorImpl& alloc_;

  // ref_count_ can be incremented as an atomic, without taking a new lock, as
  // the critical 0->1 transition occurs in makeCounter and makeGauge, which
  // already hold the lock. Increment also occurs when copying shared pointers,
  // but these are always in transition to ref-count 2 or higher, and thus
  // cannot race with a decrement to zero.
  //
  // However, we must hold alloc_.mutex_ when decrementing ref_count_ so that
  // when it hits zero we can atomically remove it from alloc_.counters_ or
  // alloc_.gauges_. We leave it atomic to avoid taking the lock on increment.
  std::atomic<uint32_t> ref_count_{0};

  std::atomic<uint16_t> flags_{0};
};

class CounterImpl : public StatsSharedImpl<Counter> {
public:
  CounterImpl(StatName name, AllocatorImpl& alloc, StatName tag_extracted_name,
              const StatNameTagVector& stat_name_tags)
      : StatsSharedImpl(name, alloc, tag_extracted_name, stat_name_tags) {}

  void removeFromSetLockHeld() EXCLUSIVE_LOCKS_REQUIRED(alloc_.mutex_) override {
    const size_t count = alloc_.counters_.erase(statName());
    ASSERT(count == 1);
  }

  // Stats::Counter
  void add(uint64_t amount) override {
    // Note that a reader may see a new value but an old pending_increment_ or
    // used(). From a system perspective this should be eventually consistent.
    value_ += amount;
    pending_increment_ += amount;
    flags_ |= Flags::Used;
  }
  void inc() override { add(1); }
  uint64_t latch() override { return pending_increment_.exchange(0); }
  void reset() override { value_ = 0; }
  uint64_t value() const override { return value_; }

private:
  std::atomic<uint64_t> value_{0};
  std::atomic<uint64_t> pending_increment_{0};
};

} // namespace Stats
} // namespace Envoy
