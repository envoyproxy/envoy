#include "common/stats/allocator_impl.h"

#include <cstdint>

#include "envoy/stats/stats.h"
#include "envoy/stats/symbol_table.h"

#include "common/common/hash.h"
#include "common/common/lock_guard.h"
#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/common/thread_annotations.h"
#include "common/common/utility.h"
#include "common/stats/metric_impl.h"
#include "common/stats/stat_merger.h"
#include "common/stats/symbol_table_impl.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Stats {

const char AllocatorImpl::DecrementToZeroSyncPoint[] = "decrement-zero";

AllocatorImpl::~AllocatorImpl() {
  ASSERT(counters_.empty());
  ASSERT(gauges_.empty());
}

#ifndef ENVOY_CONFIG_COVERAGE
void AllocatorImpl::debugPrint() {
  Thread::LockGuard lock(mutex_);
  for (Counter* counter : counters_) {
    ENVOY_LOG_MISC(info, "counter: {}", symbolTable().toString(counter->statName()));
  }
  for (Gauge* gauge : gauges_) {
    ENVOY_LOG_MISC(info, "gauge: {}", symbolTable().toString(gauge->statName()));
  }
}
#endif

// Counter and Gauge both inherit from from RefcountInterface and
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

  // Holds backing store shared by both CounterImpl and GaugeImpl. CounterImpl
  // adds another field, pending_increment_, that is not used in Gauge.
  std::atomic<uint64_t> value_{0};

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
  std::atomic<uint64_t> pending_increment_{0};
};

class GaugeImpl : public StatsSharedImpl<Gauge> {
public:
  GaugeImpl(StatName name, AllocatorImpl& alloc, StatName tag_extracted_name,
            const StatNameTagVector& stat_name_tags, ImportMode import_mode)
      : StatsSharedImpl(name, alloc, tag_extracted_name, stat_name_tags) {
    switch (import_mode) {
    case ImportMode::Accumulate:
      flags_ |= Flags::LogicAccumulate;
      break;
    case ImportMode::NeverImport:
      flags_ |= Flags::NeverImport;
      break;
    case ImportMode::Uninitialized:
      // Note that we don't clear any flag bits for import_mode==Uninitialized,
      // as we may have an established import_mode when this stat was created in
      // an alternate scope. See
      // https://github.com/envoyproxy/envoy/issues/7227.
      break;
    }
  }

  void removeFromSetLockHeld() override EXCLUSIVE_LOCKS_REQUIRED(alloc_.mutex_) {
    const size_t count = alloc_.gauges_.erase(statName());
    ASSERT(count == 1);
  }

  // Stats::Gauge
  void add(uint64_t amount) override {
    value_ += amount;
    flags_ |= Flags::Used;
  }
  void dec() override { sub(1); }
  void inc() override { add(1); }
  void set(uint64_t value) override {
    value_ = value;
    flags_ |= Flags::Used;
  }
  void sub(uint64_t amount) override {
    ASSERT(value_ >= amount);
    ASSERT(used() || amount == 0);
    value_ -= amount;
  }
  uint64_t value() const override { return value_; }

  ImportMode importMode() const override {
    if (flags_ & Flags::NeverImport) {
      return ImportMode::NeverImport;
    } else if (flags_ & Flags::LogicAccumulate) {
      return ImportMode::Accumulate;
    }
    return ImportMode::Uninitialized;
  }

  void mergeImportMode(ImportMode import_mode) override {
    ImportMode current = importMode();
    if (current == import_mode) {
      return;
    }

    switch (import_mode) {
    case ImportMode::Uninitialized:
      // mergeImportNode(ImportMode::Uninitialized) is called when merging an
      // existing stat with importMode() == Accumulate or NeverImport.
      break;
    case ImportMode::Accumulate:
      ASSERT(current == ImportMode::Uninitialized);
      flags_ |= Flags::LogicAccumulate;
      break;
    case ImportMode::NeverImport:
      ASSERT(current == ImportMode::Uninitialized);
      // A previous revision of Envoy may have transferred a gauge that it
      // thought was Accumulate. But the new version thinks it's NeverImport, so
      // we clear the accumulated value.
      value_ = 0;
      flags_ &= ~Flags::Used;
      flags_ |= Flags::NeverImport;
      break;
    }
  }
};

CounterSharedPtr AllocatorImpl::makeCounter(StatName name, StatName tag_extracted_name,
                                            const StatNameTagVector& stat_name_tags) {
  Thread::LockGuard lock(mutex_);
  ASSERT(gauges_.find(name) == gauges_.end());
  auto iter = counters_.find(name);
  if (iter != counters_.end()) {
    return CounterSharedPtr(*iter);
  }
  auto counter = CounterSharedPtr(new CounterImpl(name, *this, tag_extracted_name, stat_name_tags));
  counters_.insert(counter.get());
  return counter;
}

GaugeSharedPtr AllocatorImpl::makeGauge(StatName name, StatName tag_extracted_name,
                                        const StatNameTagVector& stat_name_tags,
                                        Gauge::ImportMode import_mode) {
  Thread::LockGuard lock(mutex_);
  ASSERT(counters_.find(name) == counters_.end());
  auto iter = gauges_.find(name);
  if (iter != gauges_.end()) {
    return GaugeSharedPtr(*iter);
  }
  auto gauge =
      GaugeSharedPtr(new GaugeImpl(name, *this, tag_extracted_name, stat_name_tags, import_mode));
  gauges_.insert(gauge.get());
  return gauge;
}

bool AllocatorImpl::isMutexLockedForTest() {
  bool locked = mutex_.tryLock();
  if (locked) {
    mutex_.unlock();
  }
  return !locked;
}

} // namespace Stats
} // namespace Envoy
