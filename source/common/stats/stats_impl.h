#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include "envoy/common/time.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Stats {

/**
 * This structure is the backing memory for both CounterImpl and GaugeImpl. It is designed so that
 * it can be allocated from shared memory if needed.
 */
struct RawStatData {
  struct Flags {
    static const uint8_t Used = 0x1;
  };

  static const size_t MAX_NAME_SIZE = 127;

  RawStatData() { memset(name_, 0, sizeof(name_)); }
  void initialize(const std::string& name);
  bool initialized() { return name_[0] != '\0'; }
  bool matches(const std::string& name);

  std::atomic<uint64_t> value_;
  std::atomic<uint64_t> pending_increment_;
  std::atomic<uint16_t> flags_;
  std::atomic<uint16_t> ref_count_;
  std::atomic<uint32_t> unused_;
  char name_[MAX_NAME_SIZE + 1];
};

/**
 * Abstract interface for allocating a RawStatData.
 */
class RawStatDataAllocator {
public:
  virtual ~RawStatDataAllocator() {}

  /**
   * @return RawStatData* a raw stat data block for a given stat name or nullptr if there is no more
   *         memory available for stats. The allocator may return a reference counted data location
   *         by name if one already exists with the same name. This is used for intra-process
   *         scope swapping as well as inter-process hot restart.
   */
  virtual RawStatData* alloc(const std::string& name) PURE;

  /**
   * Free a raw stat data block. The allocator should handle reference counting and only truly
   * free the block if it is no longer needed.
   */
  virtual void free(RawStatData& data) PURE;
};

/**
 * Counter implementation that wraps a RawStatData.
 */
class CounterImpl : public Counter {
public:
  CounterImpl(RawStatData& data, RawStatDataAllocator& alloc) : data_(data), alloc_(alloc) {}
  ~CounterImpl() { alloc_.free(data_); }

  // Stats::Counter
  void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.pending_increment_ += amount;
    data_.flags_ |= RawStatData::Flags::Used;
  }

  void inc() override { add(1); }
  uint64_t latch() override { return data_.pending_increment_.exchange(0); }
  std::string name() override { return data_.name_; }
  void reset() override { data_.value_ = 0; }
  bool used() override { return data_.flags_ & RawStatData::Flags::Used; }
  uint64_t value() override { return data_.value_; }

private:
  RawStatData& data_;
  RawStatDataAllocator& alloc_;
};

/**
 * Gauge implementation that wraps a RawStatData.
 */
class GaugeImpl : public Gauge {
public:
  GaugeImpl(RawStatData& data, RawStatDataAllocator& alloc) : data_(data), alloc_(alloc) {}
  ~GaugeImpl() { alloc_.free(data_); }

  // Stats::Gauge
  virtual void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.flags_ |= RawStatData::Flags::Used;
  }
  virtual void dec() override { sub(1); }
  virtual void inc() override { add(1); }
  virtual std::string name() override { return data_.name_; }
  virtual void set(uint64_t value) override {
    data_.value_ = value;
    data_.flags_ |= RawStatData::Flags::Used;
  }
  virtual void sub(uint64_t amount) override {
    ASSERT(data_.value_ >= amount);
    ASSERT(used());
    data_.value_ -= amount;
  }
  bool used() override { return data_.flags_ & RawStatData::Flags::Used; }
  virtual uint64_t value() override { return data_.value_; }

private:
  RawStatData& data_;
  RawStatDataAllocator& alloc_;
};

/**
 * Timer implementation for the heap.
 */
class TimerImpl : public Timer {
public:
  TimerImpl(const std::string& name, Store& parent) : name_(name), parent_(parent) {}

  // Stats::Timer
  TimespanPtr allocateSpan() override { return TimespanPtr{new TimespanImpl(*this)}; }
  std::string name() override { return name_; }

private:
  /**
   * Timespan implementation for the heap.
   */
  class TimespanImpl : public Timespan {
  public:
    TimespanImpl(TimerImpl& parent) : parent_(parent), start_(std::chrono::steady_clock::now()) {}

    // Stats::Timespan
    void complete() override { complete(parent_.name_); }
    void complete(const std::string& dynamic_name) override;

  private:
    TimerImpl& parent_;
    MonotonicTime start_;
  };

  std::string name_;
  Store& parent_;
};

/**
 * Implementation of RawStatDataAllocator that just allocates a new structure in memory and returns
 * it.
 */
class HeapRawStatDataAllocator : public RawStatDataAllocator {
public:
  // RawStatDataAllocator
  RawStatData* alloc(const std::string& name) override;
  void free(RawStatData& data) override;
};

/**
 * A stats cache template that is used by the isolated store.
 */
template <class Base, class Impl> class IsolatedStatsCache {
public:
  typedef std::function<Impl*(const std::string& name)> Allocator;

  IsolatedStatsCache(Allocator alloc) : alloc_(alloc) {}

  Base& get(const std::string& name) {
    auto stat = stats_.find(name);
    if (stat != stats_.end()) {
      return *stat->second;
    }

    Impl* new_stat = alloc_(name);
    stats_.emplace(name, std::shared_ptr<Impl>{new_stat});
    return *new_stat;
  }

  std::list<std::shared_ptr<Base>> toList() const {
    std::list<std::shared_ptr<Base>> list;
    for (auto& stat : stats_) {
      list.push_back(stat.second);
    }

    return list;
  }

private:
  std::unordered_map<std::string, std::shared_ptr<Impl>> stats_;
  Allocator alloc_;
};

/**
 * Store implementation that is isolated from other stores.
 */
class IsolatedStoreImpl : public Store {
public:
  IsolatedStoreImpl()
      : counters_([this](const std::string& name)
                      -> CounterImpl* { return new CounterImpl(*alloc_.alloc(name), alloc_); }),
        gauges_([this](const std::string& name)
                    -> GaugeImpl* { return new GaugeImpl(*alloc_.alloc(name), alloc_); }),
        timers_([this](const std::string& name)
                    -> TimerImpl* { return new TimerImpl(name, *this); }) {}

  // Stats::Scope
  Counter& counter(const std::string& name) override { return counters_.get(name); }
  void deliverHistogramToSinks(const std::string&, uint64_t) override {}
  void deliverTimingToSinks(const std::string&, std::chrono::milliseconds) override {}
  Gauge& gauge(const std::string& name) override { return gauges_.get(name); }
  Timer& timer(const std::string& name) override { return timers_.get(name); }

  // Stats::Store
  std::list<CounterSharedPtr> counters() const override { return counters_.toList(); }
  std::list<GaugeSharedPtr> gauges() const override { return gauges_.toList(); }
  ScopePtr createScope(const std::string& name) override {
    return ScopePtr{new ScopeImpl(*this, name)};
  }

private:
  struct ScopeImpl : public Scope {
    ScopeImpl(IsolatedStoreImpl& parent, const std::string& prefix)
        : parent_(parent), prefix_(prefix) {}

    // Stats::Scope
    void deliverHistogramToSinks(const std::string&, uint64_t) override {}
    void deliverTimingToSinks(const std::string&, std::chrono::milliseconds) override {}
    Counter& counter(const std::string& name) override { return parent_.counter(prefix_ + name); }
    Gauge& gauge(const std::string& name) override { return parent_.gauge(prefix_ + name); }
    Timer& timer(const std::string& name) override { return parent_.timer(prefix_ + name); }

    IsolatedStoreImpl& parent_;
    const std::string prefix_;
  };

  HeapRawStatDataAllocator alloc_;
  IsolatedStatsCache<Counter, CounterImpl> counters_;
  IsolatedStatsCache<Gauge, GaugeImpl> gauges_;
  IsolatedStatsCache<Timer, TimerImpl> timers_;
};

} // Stats
} // Envoy
