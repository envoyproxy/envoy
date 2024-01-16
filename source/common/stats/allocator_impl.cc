#include "source/common/stats/allocator_impl.h"

#include <algorithm>
#include <cstdint>

#include "envoy/stats/sink.h"
#include "envoy/stats/stats.h"

#include "source/common/common/hash.h"
#include "source/common/common/lock_guard.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/common/thread_annotations.h"
#include "source/common/common/utility.h"
#include "source/common/stats/metric_impl.h"
#include "source/common/stats/stat_merger.h"
#include "source/common/stats/symbol_table.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Stats {

const char AllocatorImpl::DecrementToZeroSyncPoint[] = "decrement-zero";

AllocatorImpl::~AllocatorImpl() {
  ASSERT(counters_.empty());
  ASSERT(gauges_.empty());

#ifndef NDEBUG
  // Move deleted stats into the sets for the ASSERTs in removeFromSetLockHeld to function.
  for (auto& counter : deleted_counters_) {
    auto insertion = counters_.insert(counter.get());
    // Assert that there were no duplicates.
    ASSERT(insertion.second);
  }
  for (auto& gauge : deleted_gauges_) {
    auto insertion = gauges_.insert(gauge.get());
    // Assert that there were no duplicates.
    ASSERT(insertion.second);
  }
  for (auto& text_readout : deleted_text_readouts_) {
    auto insertion = text_readouts_.insert(text_readout.get());
    // Assert that there were no duplicates.
    ASSERT(insertion.second);
  }
#endif
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

// Counter, Gauge and TextReadout inherit from RefcountInterface and
// Metric. MetricImpl takes care of most of the Metric API, but we need to cover
// symbolTable() here, which we don't store directly, but get it via the alloc,
// which we need in order to clean up the counter and gauge maps in that class
// when they are destroyed.
//
// We implement the RefcountInterface API to avoid weak counter and destructor overhead in
// shared_ptr.
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
  SymbolTable& symbolTable() final { return alloc_.symbolTable(); }
  bool used() const override { return flags_ & Metric::Flags::Used; }
  bool hidden() const override { return flags_ & Metric::Flags::Hidden; }

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
  virtual void removeFromSetLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(alloc_.mutex_) PURE;

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

  void removeFromSetLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(alloc_.mutex_) override {
    const size_t count = alloc_.counters_.erase(statName());
    ASSERT(count == 1);
    alloc_.sinked_counters_.erase(this);
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
    case ImportMode::HiddenAccumulate:
      flags_ |= Flags::Hidden;
      flags_ |= Flags::LogicAccumulate;
      break;
    }
  }

  void removeFromSetLockHeld() override ABSL_EXCLUSIVE_LOCKS_REQUIRED(alloc_.mutex_) {
    const size_t count = alloc_.gauges_.erase(statName());
    ASSERT(count == 1);
    alloc_.sinked_gauges_.erase(this);
  }

  // Stats::Gauge
  void add(uint64_t amount) override {
    child_value_ += amount;
    flags_ |= Flags::Used;
  }
  void dec() override { sub(1); }
  void inc() override { add(1); }
  void set(uint64_t value) override {
    child_value_ = value;
    flags_ |= Flags::Used;
  }
  void sub(uint64_t amount) override {
    ASSERT(child_value_ >= amount);
    ASSERT(used() || amount == 0);
    child_value_ -= amount;
  }
  uint64_t value() const override { return child_value_ + parent_value_; }

  // TODO(diazalan): Rename importMode and to more generic name
  ImportMode importMode() const override {
    if (flags_ & Flags::NeverImport) {
      return ImportMode::NeverImport;
    } else if ((flags_ & Flags::Hidden) && (flags_ & Flags::LogicAccumulate)) {
      return ImportMode::HiddenAccumulate;
    } else if (flags_ & Flags::LogicAccumulate) {
      return ImportMode::Accumulate;
    }
    return ImportMode::Uninitialized;
  }

  // TODO(diazalan): Rename mergeImportMode and to more generic name
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
      parent_value_ = 0;
      flags_ &= ~Flags::Used;
      flags_ |= Flags::NeverImport;
      break;
    case ImportMode::HiddenAccumulate:
      ASSERT(current == ImportMode::Uninitialized);
      flags_ |= Flags::Hidden;
      flags_ |= Flags::LogicAccumulate;
      break;
    }
  }

  void setParentValue(uint64_t value) override { parent_value_ = value; }

private:
  std::atomic<uint64_t> parent_value_{0};
  std::atomic<uint64_t> child_value_{0};
};

class TextReadoutImpl : public StatsSharedImpl<TextReadout> {
public:
  TextReadoutImpl(StatName name, AllocatorImpl& alloc, StatName tag_extracted_name,
                  const StatNameTagVector& stat_name_tags)
      : StatsSharedImpl(name, alloc, tag_extracted_name, stat_name_tags) {}

  void removeFromSetLockHeld() ABSL_EXCLUSIVE_LOCKS_REQUIRED(alloc_.mutex_) override {
    const size_t count = alloc_.text_readouts_.erase(statName());
    ASSERT(count == 1);
    alloc_.sinked_text_readouts_.erase(this);
  }

  // Stats::TextReadout
  void set(absl::string_view value) override {
    std::string value_copy(value);
    absl::MutexLock lock(&mutex_);
    value_ = std::move(value_copy);
    flags_ |= Flags::Used;
  }
  std::string value() const override {
    absl::MutexLock lock(&mutex_);
    return value_;
  }

private:
  mutable absl::Mutex mutex_;
  std::string value_ ABSL_GUARDED_BY(mutex_);
};

CounterSharedPtr AllocatorImpl::makeCounter(StatName name, StatName tag_extracted_name,
                                            const StatNameTagVector& stat_name_tags) {
  Thread::LockGuard lock(mutex_);
  ASSERT(gauges_.find(name) == gauges_.end());
  ASSERT(text_readouts_.find(name) == text_readouts_.end());
  auto iter = counters_.find(name);
  if (iter != counters_.end()) {
    return {*iter};
  }
  auto counter = CounterSharedPtr(makeCounterInternal(name, tag_extracted_name, stat_name_tags));
  counters_.insert(counter.get());
  // Add counter to sinked_counters_ if it matches the sink predicate.
  if (sink_predicates_ != nullptr && sink_predicates_->includeCounter(*counter)) {
    auto val = sinked_counters_.insert(counter.get());
    ASSERT(val.second);
  }
  return counter;
}

GaugeSharedPtr AllocatorImpl::makeGauge(StatName name, StatName tag_extracted_name,
                                        const StatNameTagVector& stat_name_tags,
                                        Gauge::ImportMode import_mode) {
  Thread::LockGuard lock(mutex_);
  ASSERT(counters_.find(name) == counters_.end());
  ASSERT(text_readouts_.find(name) == text_readouts_.end());
  auto iter = gauges_.find(name);
  if (iter != gauges_.end()) {
    return {*iter};
  }
  auto gauge =
      GaugeSharedPtr(new GaugeImpl(name, *this, tag_extracted_name, stat_name_tags, import_mode));
  gauges_.insert(gauge.get());
  // Add gauge to sinked_gauges_ if it matches the sink predicate.
  if (sink_predicates_ != nullptr && sink_predicates_->includeGauge(*gauge)) {
    auto val = sinked_gauges_.insert(gauge.get());
    ASSERT(val.second);
  }
  return gauge;
}

TextReadoutSharedPtr AllocatorImpl::makeTextReadout(StatName name, StatName tag_extracted_name,
                                                    const StatNameTagVector& stat_name_tags) {
  Thread::LockGuard lock(mutex_);
  ASSERT(counters_.find(name) == counters_.end());
  ASSERT(gauges_.find(name) == gauges_.end());
  auto iter = text_readouts_.find(name);
  if (iter != text_readouts_.end()) {
    return {*iter};
  }
  auto text_readout =
      TextReadoutSharedPtr(new TextReadoutImpl(name, *this, tag_extracted_name, stat_name_tags));
  text_readouts_.insert(text_readout.get());
  // Add text_readout to sinked_text_readouts_ if it matches the sink predicate.
  if (sink_predicates_ != nullptr && sink_predicates_->includeTextReadout(*text_readout)) {
    auto val = sinked_text_readouts_.insert(text_readout.get());
    ASSERT(val.second);
  }
  return text_readout;
}

bool AllocatorImpl::isMutexLockedForTest() {
  bool locked = mutex_.tryLock();
  if (locked) {
    mutex_.unlock();
  }
  return !locked;
}

Counter* AllocatorImpl::makeCounterInternal(StatName name, StatName tag_extracted_name,
                                            const StatNameTagVector& stat_name_tags) {
  return new CounterImpl(name, *this, tag_extracted_name, stat_name_tags);
}

void AllocatorImpl::forEachCounter(SizeFn f_size, StatFn<Counter> f_stat) const {
  Thread::LockGuard lock(mutex_);
  if (f_size != nullptr) {
    f_size(counters_.size());
  }
  for (auto& counter : counters_) {
    f_stat(*counter);
  }
}

void AllocatorImpl::forEachGauge(SizeFn f_size, StatFn<Gauge> f_stat) const {
  Thread::LockGuard lock(mutex_);
  if (f_size != nullptr) {
    f_size(gauges_.size());
  }
  for (auto& gauge : gauges_) {
    f_stat(*gauge);
  }
}

void AllocatorImpl::forEachTextReadout(SizeFn f_size, StatFn<TextReadout> f_stat) const {
  Thread::LockGuard lock(mutex_);
  if (f_size != nullptr) {
    f_size(text_readouts_.size());
  }
  for (auto& text_readout : text_readouts_) {
    f_stat(*text_readout);
  }
}

void AllocatorImpl::forEachSinkedCounter(SizeFn f_size, StatFn<Counter> f_stat) const {
  if (sink_predicates_ != nullptr) {
    Thread::LockGuard lock(mutex_);
    f_size(sinked_counters_.size());
    for (auto counter : sinked_counters_) {
      f_stat(*counter);
    }
  } else {
    forEachCounter(f_size, f_stat);
  }
}

void AllocatorImpl::forEachSinkedGauge(SizeFn f_size, StatFn<Gauge> f_stat) const {
  if (sink_predicates_ != nullptr) {
    Thread::LockGuard lock(mutex_);
    f_size(sinked_gauges_.size());
    for (auto gauge : sinked_gauges_) {
      f_stat(*gauge);
    }
  } else {
    forEachGauge(f_size, [&f_stat](Gauge& gauge) {
      if (!gauge.hidden()) {
        f_stat(gauge);
      }
    });
  }
}

void AllocatorImpl::forEachSinkedTextReadout(SizeFn f_size, StatFn<TextReadout> f_stat) const {
  if (sink_predicates_ != nullptr) {
    Thread::LockGuard lock(mutex_);
    f_size(sinked_text_readouts_.size());
    for (auto text_readout : sinked_text_readouts_) {
      f_stat(*text_readout);
    }
  } else {
    forEachTextReadout(f_size, f_stat);
  }
}

void AllocatorImpl::setSinkPredicates(std::unique_ptr<SinkPredicates>&& sink_predicates) {
  Thread::LockGuard lock(mutex_);
  ASSERT(sink_predicates_ == nullptr);
  sink_predicates_ = std::move(sink_predicates);
  sinked_counters_.clear();
  sinked_gauges_.clear();
  sinked_text_readouts_.clear();
  // Add counters to the set of sinked counters.
  for (auto& counter : counters_) {
    if (sink_predicates_->includeCounter(*counter)) {
      sinked_counters_.emplace(counter);
    }
  }
  // Add gauges to the set of sinked gauges.
  for (auto& gauge : gauges_) {
    if (sink_predicates_->includeGauge(*gauge)) {
      sinked_gauges_.insert(gauge);
    }
  }
  // Add text_readouts to the set of sinked text readouts.
  for (auto& text_readout : text_readouts_) {
    if (sink_predicates_->includeTextReadout(*text_readout)) {
      sinked_text_readouts_.insert(text_readout);
    }
  }
}

void AllocatorImpl::markCounterForDeletion(const CounterSharedPtr& counter) {
  Thread::LockGuard lock(mutex_);
  auto iter = counters_.find(counter->statName());
  if (iter == counters_.end()) {
    // This has already been marked for deletion.
    return;
  }
  ASSERT(counter.get() == *iter);
  // Duplicates are ASSERTed in ~AllocatorImpl.
  deleted_counters_.emplace_back(*iter);
  counters_.erase(iter);
  sinked_counters_.erase(counter.get());
}

void AllocatorImpl::markGaugeForDeletion(const GaugeSharedPtr& gauge) {
  Thread::LockGuard lock(mutex_);
  auto iter = gauges_.find(gauge->statName());
  if (iter == gauges_.end()) {
    // This has already been marked for deletion.
    return;
  }
  ASSERT(gauge.get() == *iter);
  // Duplicates are ASSERTed in ~AllocatorImpl.
  deleted_gauges_.emplace_back(*iter);
  gauges_.erase(iter);
  sinked_gauges_.erase(gauge.get());
}

void AllocatorImpl::markTextReadoutForDeletion(const TextReadoutSharedPtr& text_readout) {
  Thread::LockGuard lock(mutex_);
  auto iter = text_readouts_.find(text_readout->statName());
  if (iter == text_readouts_.end()) {
    // This has already been marked for deletion.
    return;
  }
  ASSERT(text_readout.get() == *iter);
  // Duplicates are ASSERTed in ~AllocatorImpl.
  deleted_text_readouts_.emplace_back(*iter);
  text_readouts_.erase(iter);
  sinked_text_readouts_.erase(text_readout.get());
}

} // namespace Stats
} // namespace Envoy
