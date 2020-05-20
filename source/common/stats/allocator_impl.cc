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

private:
  std::atomic<uint64_t> value_{0};
};

class TextReadoutImpl : public StatsSharedImpl<TextReadout> {
public:
  TextReadoutImpl(StatName name, AllocatorImpl& alloc, StatName tag_extracted_name,
                  const StatNameTagVector& stat_name_tags)
      : StatsSharedImpl(name, alloc, tag_extracted_name, stat_name_tags) {}

  void removeFromSetLockHeld() EXCLUSIVE_LOCKS_REQUIRED(alloc_.mutex_) override {
    const size_t count = alloc_.text_readouts_.erase(statName());
    ASSERT(count == 1);
  }

  // Stats::TextReadout
  void set(absl::string_view value) override {
    std::string value_copy(value);
    absl::MutexLock lock(&mutex_);
    value_ = std::move(value_copy);
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
    return CounterSharedPtr(*iter);
  }
  auto counter = CounterSharedPtr(makeCounterImpl(name, tag_extracted_name, stat_name_tags));
  counters_.insert(counter.get());
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
    return GaugeSharedPtr(*iter);
  }
  auto gauge =
      GaugeSharedPtr(new GaugeImpl(name, *this, tag_extracted_name, stat_name_tags, import_mode));
  gauges_.insert(gauge.get());
  return gauge;
}

TextReadoutSharedPtr AllocatorImpl::makeTextReadout(StatName name, StatName tag_extracted_name,
                                                    const StatNameTagVector& stat_name_tags) {
  Thread::LockGuard lock(mutex_);
  ASSERT(counters_.find(name) == counters_.end());
  ASSERT(gauges_.find(name) == gauges_.end());
  auto iter = text_readouts_.find(name);
  if (iter != text_readouts_.end()) {
    return TextReadoutSharedPtr(*iter);
  }
  auto text_readout =
      TextReadoutSharedPtr(new TextReadoutImpl(name, *this, tag_extracted_name, stat_name_tags));
  text_readouts_.insert(text_readout.get());
  return text_readout;
}

bool AllocatorImpl::isMutexLockedForTest() {
  bool locked = mutex_.tryLock();
  if (locked) {
    mutex_.unlock();
  }
  return !locked;
}

CounterImpl* AllocatorImpl::makeCounterImpl(StatName name, StatName tag_extracted_name,
                                            const StatNameTagVector& stat_name_tags) {
  return new CounterImpl(name, *this, tag_extracted_name, stat_name_tags);
}

} // namespace Stats
} // namespace Envoy
