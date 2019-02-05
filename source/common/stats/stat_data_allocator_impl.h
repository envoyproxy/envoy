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
  // StatDataAllocator
  CounterSharedPtr makeCounter(absl::string_view name, std::string&& tag_extracted_name,
                               std::vector<Tag>&& tags) override;
  GaugeSharedPtr makeGauge(absl::string_view name, std::string&& tag_extracted_name,
                           std::vector<Tag>&& tags) override;
  TextReadoutSharedPtr makeTextReadout(absl::string_view name, std::string&& tag_extracted_name,
                                       std::vector<Tag>&& tags) override;

  /**
   * @param name the full name of the stat.
   * @return StatData* a data block for a given stat name or nullptr if there is no more memory
   *         available for stats. The allocator should return a reference counted data location
   *         by name if one already exists with the same name. This is used for intra-process
   *         scope swapping as well as inter-process hot restart.
   */
  virtual StatData* alloc(absl::string_view name) PURE;

  /**
   * Free a raw stat data block. The allocator should handle reference counting and only truly
   * free the block if it is no longer needed.
   * @param data the data returned by alloc().
   */
  virtual void free(StatData& data) PURE;
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
              std::string&& tag_extracted_name, std::vector<Tag>&& tags)
      : MetricImpl(std::move(tag_extracted_name), std::move(tags)), data_(data), alloc_(alloc) {}
  ~CounterImpl() { alloc_.free(data_); }

  // Stats::Metric
  std::string name() const override { return std::string(data_.name()); }
  const char* nameCStr() const override { return data_.name(); }
  bool used() const override { return data_.flags_ & Flags::Used; }

  // Stats::Counter
  void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.pending_increment_ += amount;
    data_.flags_ |= Flags::Used;
  }

  void inc() override { add(1); }
  uint64_t latch() override { return data_.pending_increment_.exchange(0); }
  void reset() override { data_.value_ = 0; }
  uint64_t value() const override { return data_.value_; }

private:
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
  std::string name() const override { return ""; }
  const char* nameCStr() const override { return ""; }
  const std::string& tagExtractedName() const override { CONSTRUCT_ON_FIRST_USE(std::string, ""); }
  const std::vector<Tag>& tags() const override { CONSTRUCT_ON_FIRST_USE(std::vector<Tag>, {}); }
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
            std::string&& tag_extracted_name, std::vector<Tag>&& tags)
      : MetricImpl(std::move(tag_extracted_name), std::move(tags)), data_(data), alloc_(alloc) {}
  ~GaugeImpl() { alloc_.free(data_); }

  // Stats::Metric
  std::string name() const override { return std::string(data_.name()); }
  const char* nameCStr() const override { return data_.name(); }
  bool used() const override { return data_.flags_ & Flags::Used; }

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

private:
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
  const char* nameCStr() const override { return ""; }
  const std::string& tagExtractedName() const override { CONSTRUCT_ON_FIRST_USE(std::string, ""); }
  const std::vector<Tag>& tags() const override { CONSTRUCT_ON_FIRST_USE(std::vector<Tag>, {}); }
  void add(uint64_t) override {}
  void inc() override {}
  void dec() override {}
  void set(uint64_t) override {}
  void sub(uint64_t) override {}
  bool used() const override { return false; }
  uint64_t value() const override { return 0; }
};

/**
 * TextReadout implementation that wraps a StatData. Max length 15; set() will truncate.
 */
template <class StatData> class TextReadoutImpl : public TextReadout, public MetricImpl {
public:
  TextReadoutImpl(StatData& data, StatDataAllocatorImpl<StatData>& alloc,
                  std::string&& tag_extracted_name, std::vector<Tag>&& tags)
      : MetricImpl(std::move(tag_extracted_name), std::move(tags)), data_(data), alloc_(alloc) {}
  ~TextReadoutImpl() { alloc_.free(data_); }

  // Stats::Metric
  std::string name() const override { return std::string(data_.name()); }
  const char* nameCStr() const override { return data_.name(); }
  bool used() const override { return data_.flags_ & Flags::Used; }

  // Stats::TextReadout
  virtual void set(const std::string& value) override {
    uint64_t data1 = 0;
    uint64_t data2 = 0;
    uint8_t* data1_ptr = reinterpret_cast<uint8_t*>(&data1);
    uint8_t* data2_ptr = reinterpret_cast<uint8_t*>(&data2);
    uint8_t total_length = static_cast<uint8_t>(value.length() <= 15 ? value.length() : 15);
    int length1 = total_length <= 7 ? total_length : 7;
    int length2 = total_length - length1;
    data1_ptr[0] = total_length;
    memcpy(data1_ptr + 1, value.data(), length1);
    if (total_length > 7) {
      memcpy(data2_ptr, value.data() + 7, length2);
    }
    data_.value_.store(data1);
    data_.pending_increment_.store(data2);
    data_.flags_ |= Flags::Used;
  }
  virtual std::string value() const override {
    uint64_t data1 = data_.value_.load();
    uint64_t data2 = data_.pending_increment_.load();
    uint8_t* data1_ptr = reinterpret_cast<uint8_t*>(&data1);
    uint8_t* data2_ptr = reinterpret_cast<uint8_t*>(&data2);
    uint8_t total_length = data1_ptr[0];
    char buf[15];
    memcpy(buf, data1_ptr + 1, 7);
    memcpy(buf + 7, data2_ptr, 8);
    return std::string(buf, total_length);
  }

private:
  StatData& data_;
  StatDataAllocatorImpl<StatData>& alloc_;
};

/**
 * Null text readout implementation.
 * No-ops on all calls and requires no underlying metric or data.
 */
class NullTextReadoutImpl : public TextReadout {
public:
  NullTextReadoutImpl() {}
  ~NullTextReadoutImpl() {}
  std::string name() const override { return ""; }
  const char* nameCStr() const override { return ""; }
  const std::string& tagExtractedName() const override { CONSTRUCT_ON_FIRST_USE(std::string, ""); }
  const std::vector<Tag>& tags() const override { CONSTRUCT_ON_FIRST_USE(std::vector<Tag>, {}); }
  void set(const std::string&) override {}
  bool used() const override { return false; }
  std::string value() const override { return std::string(); }
};

template <class StatData>
CounterSharedPtr StatDataAllocatorImpl<StatData>::makeCounter(absl::string_view name,
                                                              std::string&& tag_extracted_name,
                                                              std::vector<Tag>&& tags) {
  StatData* data = alloc(name);
  if (data == nullptr) {
    return nullptr;
  }
  return std::make_shared<CounterImpl<StatData>>(*data, *this, std::move(tag_extracted_name),
                                                 std::move(tags));
}

template <class StatData>
GaugeSharedPtr StatDataAllocatorImpl<StatData>::makeGauge(absl::string_view name,
                                                          std::string&& tag_extracted_name,
                                                          std::vector<Tag>&& tags) {
  StatData* data = alloc(name);
  if (data == nullptr) {
    return nullptr;
  }
  return std::make_shared<GaugeImpl<StatData>>(*data, *this, std::move(tag_extracted_name),
                                               std::move(tags));
}

template <class StatData>
TextReadoutSharedPtr StatDataAllocatorImpl<StatData>::makeTextReadout(
    absl::string_view name, std::string&& tag_extracted_name, std::vector<Tag>&& tags) {
  StatData* data = alloc(name);
  if (data == nullptr) {
    return nullptr;
  }
  return std::make_shared<TextReadoutImpl<StatData>>(*data, *this, std::move(tag_extracted_name),
                                                     std::move(tags));
}

} // namespace Stats
} // namespace Envoy
