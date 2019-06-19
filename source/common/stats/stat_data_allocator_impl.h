#pragma once

#include <string>
#include <vector>

#include "envoy/stats/stat_data_allocator.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/symbol_table.h"

#include "common/common/assert.h"
#include "common/stats/metric_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Stats {

// Partially implements a StatDataAllocator, leaving alloc & free for subclasses.
// We templatize on StatData rather than defining a virtual base StatData class
// for performance reasons; stat increment is on the hot path.
//
// The two production derivations cover using a fixed block of shared-memory for
// hot restart stat continuity, and heap allocation for more efficient RAM usage
// for when hot-restart is not required.
//
// TODO(fredlas) the above paragraph is obsolete; it's now only heap. So, this
// interface can hopefully be collapsed down a bit.
template <class StatData> class StatDataAllocatorImpl : public StatDataAllocator {
public:
  explicit StatDataAllocatorImpl(SymbolTable& symbol_table) : symbol_table_(symbol_table) {}

  /**
   * Free a raw stat data block. The allocator should handle reference counting and only truly
   * free the block if it is no longer needed.
   * @param data the data returned by alloc().
   */
  virtual void free(StatData& data) PURE;

  SymbolTable& symbolTable() override { return symbol_table_; }
  const SymbolTable& constSymbolTable() const override { return symbol_table_; }

private:
  // SymbolTable encodes stat names as back into strings. This does not
  // get guarded by a mutex, since it has its own internal mutex to guarantee
  // thread safety.
  SymbolTable& symbol_table_;
};

/**
 * Counter implementation that wraps a StatData. StatData must have data members:
 *    std::atomic<int64_t> value_;
 *    std::atomic<int64_t> pending_increment_;
 *    std::atomic<int16_t> flags_;
 *    std::atomic<int16_t> ref_count_;
 */
template <class StatData> class CounterImpl : public Counter, public MetricImpl {
public:
  CounterImpl(StatData& data, StatDataAllocatorImpl<StatData>& alloc,
              absl::string_view tag_extracted_name, const std::vector<Tag>& tags)
      : MetricImpl(tag_extracted_name, tags, alloc.symbolTable()), data_(data), alloc_(alloc) {}
  ~CounterImpl() override {
    alloc_.free(data_);

    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    MetricImpl::clear();
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

protected:
  StatData& data_;
  StatDataAllocatorImpl<StatData>& alloc_;
};

/**
 * Null counter implementation.
 * No-ops on all calls and requires no underlying metric or data.
 */
class NullCounterImpl : public Counter, NullMetricImpl {
public:
  explicit NullCounterImpl(SymbolTable& symbol_table) : NullMetricImpl(symbol_table) {}
  ~NullCounterImpl() override {
    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    MetricImpl::clear();
  }

  void add(uint64_t) override {}
  void inc() override {}
  uint64_t latch() override { return 0; }
  void reset() override {}
  uint64_t value() const override { return 0; }
};

/**
 * Gauge implementation that wraps a StatData.
 */
template <class StatData> class GaugeImpl : public Gauge, public MetricImpl {
public:
  GaugeImpl(StatData& data, StatDataAllocatorImpl<StatData>& alloc,
            absl::string_view tag_extracted_name, const std::vector<Tag>& tags,
            ImportMode import_mode)
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
    alloc_.free(data_);

    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    MetricImpl::clear();
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

protected:
  StatData& data_;
  StatDataAllocatorImpl<StatData>& alloc_;
};

/**
 * Null gauge implementation.
 * No-ops on all calls and requires no underlying metric or data.
 */
class NullGaugeImpl : public Gauge, NullMetricImpl {
public:
  explicit NullGaugeImpl(SymbolTable& symbol_table) : NullMetricImpl(symbol_table) {}
  ~NullGaugeImpl() override {
    // MetricImpl must be explicitly cleared() before destruction, otherwise it
    // will not be able to access the SymbolTable& to free the symbols. An RAII
    // alternative would be to store the SymbolTable reference in the
    // MetricImpl, costing 8 bytes per stat.
    MetricImpl::clear();
  }

  void add(uint64_t) override {}
  void inc() override {}
  void dec() override {}
  void set(uint64_t) override {}
  void sub(uint64_t) override {}
  uint64_t value() const override { return 0; }
  ImportMode importMode() const override { return ImportMode::NeverImport; }
  void mergeImportMode(ImportMode /* import_mode */) override {}
};

} // namespace Stats
} // namespace Envoy
