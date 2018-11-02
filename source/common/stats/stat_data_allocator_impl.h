#pragma once

#include <string>
#include <vector>

#include "envoy/stats/stat_data_allocator.h"
#include "envoy/stats/stats.h"

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
// Also note that RawStatData needs to live in a shared memory block, and it's
// possible, but not obvious, that a vptr would be usable across processes. In
// any case, RawStatData is allocated from a shared-memory block rather than via
// new, so the usual C++ compiler assistance for setting up vptrs will not be
// available. This could be resolved with placed new, or another nesting level.
template <class StatData> class StatDataAllocatorImpl : public StatDataAllocator {
public:
  explicit StatDataAllocatorImpl(SymbolTable& symbol_table) : symbol_table_(symbol_table) {}

  /**
   * @param name the full name of the stat.
   * @return StatData* a data block for a given stat name or nullptr if there is no more memory
   *         available for stats. The allocator should return a reference counted data location
   *         by name if one already exists with the same name. This is used for intra-process
   *         scope swapping as well as inter-process hot restart.
   */
  //virtual StatData* alloc(StatName name) PURE;

  /**
   * Free a raw stat data block. The allocator should handle reference counting and only truly
   * free the block if it is no longer needed.
   * @param data the data returned by alloc().
   */
  virtual void free(StatData& data) PURE;

  SymbolTable& symbolTable() override { return symbol_table_; }
  const SymbolTable& symbolTable() const override { return symbol_table_; }

 private:
  // SymbolTable encodes encodes stat names as back into strings. This does not
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
  ~CounterImpl() { alloc_.free(data_); }

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

  StatDataAllocator& allocator() override { return alloc_; }
  const StatDataAllocator& allocator() const override { return alloc_; }

protected:
  StatData& data_;
  StatDataAllocatorImpl<StatData>& alloc_;
};

/**
 * Null counter implementation.
 * No-ops on all calls and requires no underlying metric or data.
 */
class NullCounterImpl : public Counter {
public:
  NullCounterImpl() {}
  ~NullCounterImpl() {}

  // Stats::Metric
  std::string name() const override { return ""; }
  StatName statName() const override { return StatName(); }

  std::string tagExtractedName() const override { return ""; }
  std::vector<Tag> tags() const override { return std::vector<Tag>(); }
  void add(uint64_t) override {}
  void inc() override {}
  uint64_t latch() override { return 0; }
  void reset() override {}
  bool used() const override { return false; }
  uint64_t value() const override { return 0; }
};

/**
 * Gauge implementation that wraps a StatData.
 */
template <class StatData> class GaugeImpl : public Gauge, public MetricImpl {
public:
  GaugeImpl(StatData& data, StatDataAllocatorImpl<StatData>& alloc,
            absl::string_view tag_extracted_name, const std::vector<Tag>& tags)
      : MetricImpl(tag_extracted_name, tags, alloc.symbolTable()), data_(data), alloc_(alloc) {}
  ~GaugeImpl() { alloc_.free(data_); }

  // Stats::Gauge
  virtual void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.flags_ |= Flags::Used;
  }
  virtual void dec() override { sub(1); }
  virtual void inc() override { add(1); }
  virtual void set(uint64_t value) override {
    data_.value_ = value;
    data_.flags_ |= Flags::Used;
  }
  virtual void sub(uint64_t amount) override {
    ASSERT(data_.value_ >= amount);
    ASSERT(used());
    data_.value_ -= amount;
  }
  virtual uint64_t value() const override { return data_.value_; }
  bool used() const override { return data_.flags_ & Flags::Used; }

  StatDataAllocator& allocator() override { return alloc_; }
  const StatDataAllocator& allocator() const override { return alloc_; }

protected:
  StatData& data_;
  StatDataAllocatorImpl<StatData>& alloc_;
};

/**
 * Null gauge implementation.
 * No-ops on all calls and requires no underlying metric or data.
 */
class NullGaugeImpl : public Gauge {
public:
  NullGaugeImpl() {}
  ~NullGaugeImpl() {}
  std::string name() const override { return ""; }
  StatName statName() const override { return StatName(); }
  std::string tagExtractedName() const override { return ""; }
  std::vector<Tag> tags() const override { return std::vector<Tag>(); }
  void add(uint64_t) override {}
  void inc() override {}
  void dec() override {}
  void set(uint64_t) override {}
  void sub(uint64_t) override {}
  bool used() const override { return false; }
  uint64_t value() const override { return 0; }
};

} // namespace Stats
} // namespace Envoy
