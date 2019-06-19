#include "common/stats/heap_stat_data.h"

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

HeapStatDataAllocator::~HeapStatDataAllocator() { ASSERT(stats_.empty()); }

HeapStatData* HeapStatData::alloc(StatName stat_name, SymbolTable& symbol_table) {
  symbol_table.incRefCount(stat_name);
  return new (stat_name.size()) HeapStatData(stat_name);
}

void HeapStatData::free(SymbolTable& symbol_table) {
  symbol_table.free(statName());
  delete this;
}

HeapStatData& HeapStatDataAllocator::alloc(StatName name) {
  using HeapStatDataFreeFn = std::function<void(HeapStatData * d)>;
  std::unique_ptr<HeapStatData, HeapStatDataFreeFn> data_ptr(
      HeapStatData::alloc(name, symbolTable()),
      [this](HeapStatData* d) { d->free(symbolTable()); });
  Thread::ReleasableLockGuard lock(mutex_);
  auto ret = stats_.insert(data_ptr.get());
  HeapStatData* existing_data = *ret.first;
  lock.release();

  if (ret.second) {
    return *data_ptr.release();
  }
  ++existing_data->ref_count_;
  return *existing_data;
}

void HeapStatDataAllocator::free(HeapStatData& data) {
  ASSERT(data.ref_count_ > 0);
  if (--data.ref_count_ > 0) {
    return;
  }

  {
    Thread::LockGuard lock(mutex_);
    size_t key_removed = stats_.erase(&data);
    ASSERT(key_removed == 1);
  }

  data.free(symbolTable());
}

#ifndef ENVOY_CONFIG_COVERAGE
void HeapStatDataAllocator::debugPrint() {
  Thread::LockGuard lock(mutex_);
  for (HeapStatData* heap_stat_data : stats_) {
    ENVOY_LOG_MISC(info, "{}", symbolTable().toString(heap_stat_data->statName()));
  }
}
#endif

class CounterImpl : public Counter, public MetricImpl {
public:
  CounterImpl(HeapStatData& data, HeapStatDataAllocator& alloc,
              absl::string_view tag_extracted_name, const std::vector<Tag>& tags)
      : MetricImpl(tag_extracted_name, tags, alloc.symbolTable()), data_(data), alloc_(alloc) {}

  ~CounterImpl() override {
    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    MetricImpl::clear();
    alloc_.free(data_);
  }

  // Stats::Counter
  void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.pending_increment_ += amount;
    data_.flags_ |= Flags::Used;
  }
  void inc() override { add(1); }
  uint64_t latch() override { return data_.pending_increment_.exchange(0); }
  void reset() override { data_.value_ = 0; }
  bool used() const override { return data_.flags_ & Flags::Used; }
  uint64_t value() const override { return data_.value_; }

  SymbolTable& symbolTable() override { return alloc_.symbolTable(); }
  StatName statName() const override { return data_.statName(); }

private:
  HeapStatData& data_;
  HeapStatDataAllocator& alloc_;
};

class GaugeImpl : public Gauge, public MetricImpl {
public:
  GaugeImpl(HeapStatData& data, HeapStatDataAllocator& alloc, absl::string_view tag_extracted_name,
            const std::vector<Tag>& tags, ImportMode import_mode)
      : MetricImpl(tag_extracted_name, tags, alloc.symbolTable()), data_(data), alloc_(alloc) {
    switch (import_mode) {
    case ImportMode::Accumulate:
      data_.flags_ |= Flags::LogicAccumulate;
      break;
    case ImportMode::NeverImport:
      data_.flags_ |= Flags::NeverImport;
      break;
    case ImportMode::Uninitialized:
      // Note that we don't clear any flag bits for import_mode==Uninitialized,
      // as we may have an established import_mode when this stat was created in
      // an alternate scope. See
      // https://github.com/envoyproxy/envoy/issues/7227.
      break;
    }
  }

  ~GaugeImpl() override {
    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    MetricImpl::clear();
    alloc_.free(data_);
  }

  // Stats::Gauge
  void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.flags_ |= Flags::Used;
  }
  void dec() override { sub(1); }
  void inc() override { add(1); }
  void set(uint64_t value) override {
    data_.value_ = value;
    data_.flags_ |= Flags::Used;
  }
  void sub(uint64_t amount) override {
    ASSERT(data_.value_ >= amount);
    ASSERT(used() || amount == 0);
    data_.value_ -= amount;
  }
  uint64_t value() const override { return data_.value_; }
  bool used() const override { return data_.flags_ & Flags::Used; }

  ImportMode importMode() const override {
    if (data_.flags_ & Flags::NeverImport) {
      return ImportMode::NeverImport;
    } else if (data_.flags_ & Flags::LogicAccumulate) {
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
      data_.flags_ |= Flags::LogicAccumulate;
      break;
    case ImportMode::NeverImport:
      ASSERT(current == ImportMode::Uninitialized);
      // A previous revision of Envoy may have transferred a gauge that it
      // thought was Accumulate. But the new version thinks it's NeverImport, so
      // we clear the accumulated value.
      data_.value_ = 0;
      data_.flags_ &= ~Flags::Used;
      data_.flags_ |= Flags::NeverImport;
      break;
    }
  }

  SymbolTable& symbolTable() override { return alloc_.symbolTable(); }
  StatName statName() const override { return data_.statName(); }

private:
  HeapStatData& data_;
  HeapStatDataAllocator& alloc_;
};

CounterSharedPtr HeapStatDataAllocator::makeCounter(StatName name,
                                                    absl::string_view tag_extracted_name,
                                                    const std::vector<Tag>& tags) {
  return std::make_shared<CounterImpl>(alloc(name), *this, tag_extracted_name, tags);
}

GaugeSharedPtr HeapStatDataAllocator::makeGauge(StatName name, absl::string_view tag_extracted_name,
                                                const std::vector<Tag>& tags,
                                                Gauge::ImportMode import_mode) {
  return std::make_shared<GaugeImpl>(alloc(name), *this, tag_extracted_name, tags, import_mode);
}

} // namespace Stats
} // namespace Envoy
