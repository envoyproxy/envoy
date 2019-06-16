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

#ifndef ENVOY_CONFIG_COVERAGE
void HeapStatDataAllocator::debugPrint() {
  /*
  Thread::LockGuard lock(mutex_);
  for (HeapStatData* heap_stat_data : stats_) {
    ENVOY_LOG_MISC(info, "{}", symbolTable().toString(heap_stat_data->statName()));
  }
  */
}
#endif

class CounterImpl : public Counter, public MetricImpl /*, public InlineStorage*/ {
public:
  static CounterImpl* create(StatName stat_name, HeapStatDataAllocator& alloc,
                             absl::string_view tag_extracted_name, const std::vector<Tag>& tags) {
    // return new (stat_name.size() + sizeof(CounterImpl)) CounterImpl(
    //    stat_name, alloc, tag_extracted_name, tags);
    return new CounterImpl(stat_name, alloc, tag_extracted_name, tags);
  }

  ~CounterImpl() override {
    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    MetricImpl::clear();
    // symbolTable().free(statName());
    // symbol_storage_.free(symbolTable());
    alloc_.freeCounter(stat_name_);
  }

  // Stats::Counter
  void add(uint64_t amount) override {
    value_ += amount;
    pending_increment_ += amount;
    used_ = true;
  }
  void inc() override { add(1); }
  uint64_t latch() override { return pending_increment_.exchange(0); }
  void reset() override { value_ = 0; }
  bool used() const override { return used_; }
  uint64_t value() const override { return value_; }

  SymbolTable& symbolTable() override { return alloc_.symbolTable(); }
  // StatName statName() const override { return StatName(symbol_storage_); }
  StatName statName() const override { return stat_name_; }

private:
  CounterImpl(StatName stat_name, HeapStatDataAllocator& alloc,
              absl::string_view tag_extracted_name, const std::vector<Tag>& tags)
      : MetricImpl(tag_extracted_name, tags, alloc.symbolTable()), alloc_(alloc),
        stat_name_(stat_name) {
    // stat_name.copyToStorage(symbol_storage_);
  }

  std::atomic<uint64_t> value_{0};
  std::atomic<uint64_t> pending_increment_{0}; // Can this be a uint32_t?
  std::atomic<bool> used_{false};

  HeapStatDataAllocator& alloc_;

  // TODO(jmarantz): it does not work to use a variable-length array here because
  // of the virtual inheritance in parent class 'Metric'. We get error:
  //    error: flexible array member 'symbol_storage_' not allowed in class which has a virtual base
  //    class
  // This should be resolved to try to inline the memory, possibly by using
  // delegation instead of virtual inheritance.
  // SymbolTable::Storage symbol_storage_; // This is a 'using' nickname for uint8_t[].
  // StatNameStorage symbol_storage_;
  StatName stat_name_;
};

class GaugeImpl : public Gauge, public MetricImpl /*, public InlineStorage */ {
public:
  static GaugeImpl* create(StatName stat_name, HeapStatDataAllocator& alloc,
                           absl::string_view tag_extracted_name, const std::vector<Tag>& tags,
                           ImportMode import_mode) {
    // return new (stat_name.size() + sizeof GaugeImpl) GaugeImpl(
    //    stat_name, alloc, tag_extracted_named, tags, import_mode);
    return new GaugeImpl(stat_name, alloc, tag_extracted_name, tags, import_mode);
  }

  ~GaugeImpl() override {
    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    MetricImpl::clear();
    // symbolTable().free(statName());
    // symbol_storage_.free(symbolTable());
    alloc_.freeGauge(stat_name_);
  }

  // Stats::Gauge
  void add(uint64_t amount) override {
    value_ += amount;
    used_ = true;
  }
  void dec() override { sub(1); }
  void inc() override { add(1); }
  void set(uint64_t value) override {
    value_ = value;
    used_ = true;
  }
  void sub(uint64_t amount) override {
    ASSERT(value_ >= amount);
    ASSERT(used() || amount == 0);
    value_ -= amount;
  }
  uint64_t value() const override { return value_; }
  bool used() const override { return used_; }

  ImportMode importMode() const override {
    return static_cast<ImportMode>(static_cast<uint8_t>(import_mode_));
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
      import_mode_ = static_cast<uint8_t>(ImportMode::Accumulate);
      break;
    case ImportMode::NeverImport:
      ASSERT(current == ImportMode::Uninitialized);
      // A previous revision of Envoy may have transferred a gauge that it
      // thought was Accumulate. But the new version thinks it's NeverImport, so
      // we clear the accumulated value.
      value_ = 0;
      used_ = false;
      import_mode_ = static_cast<uint8_t>(ImportMode::NeverImport);
      break;
    }
  }

  SymbolTable& symbolTable() override { return alloc_.symbolTable(); }
  // StatName statName() const { return StatName(symbol_storage_); }
  StatName statName() const override { return stat_name_; }

private:
  GaugeImpl(StatName stat_name, HeapStatDataAllocator& alloc, absl::string_view tag_extracted_name,
            const std::vector<Tag>& tags, ImportMode import_mode)
      : MetricImpl(tag_extracted_name, tags, alloc.symbolTable()), alloc_(alloc),
        stat_name_(stat_name) {
    import_mode_ = static_cast<uint8_t>(import_mode);
    // stat_name.copyToStorage(symbol_storage_);
  }

  std::atomic<uint64_t> value_{0};
  std::atomic<uint8_t> import_mode_{static_cast<uint8_t>(ImportMode::Uninitialized)};
  std::atomic<bool> used_{false};

  HeapStatDataAllocator& alloc_;
  // SymbolTable::Storage symbol_storage_; // This is a 'using' nickname for uint8_t[].
  StatName stat_name_;
};

HeapStatDataAllocator::~HeapStatDataAllocator() {
  ASSERT(counters_.empty());
  ASSERT(gauges_.empty());
}

CounterSharedPtr HeapStatDataAllocator::makeCounter(StatName name,
                                                    absl::string_view tag_extracted_name,
                                                    const std::vector<Tag>& tags) {
  Thread::LockGuard lock(mutex_);
  CounterSharedPtr counter;
  ASSERT(gauges_.find(name) == gauges_.end());
  auto iter = counters_.find(name);
  if (iter != counters_.end()) {
    counter = iter->second.lock();
  }
  if (counter == nullptr) {
    uint8_t* storage = new uint8_t[name.size()];
    name.copyToStorage(storage);
    std::weak_ptr<Counter>& weak_ref = counters_[storage];
    counter.reset(CounterImpl::create(StatName(storage), *this, tag_extracted_name, tags));
    weak_ref = counter;
  }
  return counter;
}

GaugeSharedPtr HeapStatDataAllocator::makeGauge(StatName name, absl::string_view tag_extracted_name,
                                                const std::vector<Tag>& tags,
                                                Gauge::ImportMode import_mode) {
  Thread::LockGuard lock(mutex_);
  GaugeSharedPtr gauge;
  ASSERT(counters_.find(name) == counters_.end());
  auto iter = gauges_.find(name);
  if (iter != gauges_.end()) {
    gauge = iter->second.lock();
  }
  if (gauge == nullptr) {
    uint8_t* storage = new uint8_t[name.size()];
    name.copyToStorage(storage);
    std::weak_ptr<Gauge>& weak_ref = gauges_[storage];
    gauge.reset(GaugeImpl::create(StatName(storage), *this, tag_extracted_name, tags, import_mode));
    weak_ref = gauge;
  }
  return gauge;
}

void HeapStatDataAllocator::freeCounter(StatName stat_name) {
  uint8_t* bytes;
  {
    Thread::LockGuard lock(mutex_);
    auto iter = counters_.find(stat_name);
    ASSERT(iter != counters_.end());
    bytes = iter->first;
    counters_.erase(stat_name);
  }
  symbol_table_.free(stat_name);
  delete[] bytes;
}

void HeapStatDataAllocator::freeGauge(StatName stat_name) {
  uint8_t* bytes;
  {
    Thread::LockGuard lock(mutex_);
    auto iter = gauges_.find(stat_name);
    ASSERT(iter != gauges_.end());
    bytes = iter->first;
    gauges_.erase(stat_name);
  }
  symbol_table_.free(stat_name);
  delete[] bytes;
}

} // namespace Stats
} // namespace Envoy
