#pragma once

#include "envoy/common/optional.h"
#include "envoy/common/time.h"
#include "envoy/stats/stats.h"
#include "envoy/thread/thread.h"

#include "common/common/assert.h"

namespace Stats {

/**
 * This structure is the backing memory for both CounterImpl and GaugeImpl. It is designed so that
 * it can be allocated from shared memory if needed.
 */
struct RawStatData {
  static const size_t MAX_NAME_SIZE = 127;

  RawStatData() { memset(name_, 0, sizeof(name_)); }
  void initialize(const std::string& name);
  bool initialized() { return name_[0] != '\0'; }
  bool matches(const std::string& name);

  std::atomic<uint64_t> value_{0};
  std::atomic<uint64_t> pending_increment_{0};
  std::atomic<bool> used_{};
  char name_[MAX_NAME_SIZE + 1];
};

/**
 * Counter implementation that wraps a RawStatData.
 */
class CounterImpl : public Counter {
public:
  CounterImpl(RawStatData& data) : data_(data) {}

  // Stats::Counter
  void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.pending_increment_ += amount;
    data_.used_ = true;
  }

  void inc() override { add(1); }
  uint64_t latch() override { return data_.pending_increment_.exchange(0); }
  std::string name() override { return data_.name_; }
  void reset() override { data_.value_ = 0; }
  bool used() override { return data_.used_; }
  uint64_t value() override { return data_.value_; }

private:
  RawStatData& data_;
};

/**
 * Gauge implementation that wraps a RawStatData.
 */
class GaugeImpl : public Gauge {
public:
  GaugeImpl(RawStatData& data) : data_(data) {}

  // Stats::Gauge
  virtual void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.used_ = true;
  }
  virtual void dec() override { sub(1); }
  virtual void inc() override { add(1); }
  virtual std::string name() { return data_.name_; }
  virtual void set(uint64_t value) override {
    data_.value_ = value;
    data_.used_ = true;
  }
  virtual void sub(uint64_t amount) override {
    ASSERT(data_.value_ >= amount);
    data_.value_ -= amount;
    data_.used_ = true;
  }
  bool used() override { return data_.used_; }
  virtual uint64_t value() override { return data_.value_; }

private:
  RawStatData& data_;
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
    TimespanImpl(TimerImpl& parent) : parent_(parent), start_(std::chrono::system_clock::now()) {}

    // Stats::Timespan
    void complete() override { complete(parent_.name_); }
    void complete(const std::string& dynamic_name) override;

  private:
    TimerImpl& parent_;
    SystemTime start_;
  };

  std::string name_;
  Store& parent_;
};

/**
 * This is a templated class that allows stats to be allocated and then shared in thread local
 * caches to avoid a central lock when dealing with dynamic stats.
 */
template <class Impl> class StatThreadLocalCache {
public:
  typedef std::function<Impl*(const std::string& name)> Allocator;

  StatThreadLocalCache(Thread::BasicLockable& lock, Allocator alloc) : lock_(lock), alloc_(alloc) {}

  /**
   * Get an allocated stat. It will either by pulled from the thread local cache, found in the
   * central store and added to the thread local cache, or allocated and added.
   */
  Impl& get(const std::string& name) {
    static thread_local std::unordered_map<std::string, Impl*> cache_;

    // First see if we already have it in the cache, if so just return it.
    auto stat = cache_.find(name);
    if (stat != cache_.end()) {
      return *stat->second;
    }

    // Now see if it already exists in the central store. If so, we use that.
    std::unique_lock<Thread::BasicLockable> lock(lock_);
    Impl* stat_to_cache = nullptr;
    for (auto& stat : stats_) {
      if (stat->name() == name) {
        stat_to_cache = stat.get();
        break;
      }
    }

    // If we still don't have it, allocate it.
    if (!stat_to_cache) {
      stats_.emplace_back(alloc_(name));
      stat_to_cache = stats_.back().get();
    }

    // Cache and return what we found or allocated.
    cache_[name] = stat_to_cache;
    return *stat_to_cache;
  }

  /**
   * Convert the local list into a reference wrapper list.
   */
  template <class T> std::list<std::reference_wrapper<T>> toList() const {
    std::list<std::reference_wrapper<T>> list;
    std::unique_lock<Thread::BasicLockable> lock(lock_);
    for (const std::unique_ptr<Impl>& stat : stats_) {
      list.push_back(*stat);
    }

    return list;
  }

private:
  Thread::BasicLockable& lock_;
  std::list<std::unique_ptr<Impl>> stats_;
  Allocator alloc_;
};

/**
 * Abstract interface for allocating a RawStatData.
 */
class RawStatDataAllocator {
public:
  virtual ~RawStatDataAllocator() {}

  /**
   * @return RawStatData* a raw stat data block for a given stat name or nullptr if there is no more
   *         memory available for stats.
   */
  virtual RawStatData* alloc(const std::string& name) PURE;
};

/**
 * Store implementation with thread local caching.
 */
class ThreadLocalStoreImpl : public Store {
public:
  ThreadLocalStoreImpl(Thread::BasicLockable& lock, RawStatDataAllocator& alloc)
      : alloc_(alloc),
        counters_(lock, [this](const std::string& name)
                            -> CounterImpl* { return new CounterImpl(safeAlloc(name)); }),
        gauges_(lock, [this](const std::string& name)
                          -> GaugeImpl* { return new GaugeImpl(safeAlloc(name)); }),
        timers_(lock, [this](const std::string& name) -> TimerImpl* {
          return new TimerImpl(name, *this);
        }), num_last_resort_stats_(counter("stats.overflow")) {
    last_resort_stat_data_.initialize("stats.last_resort_trap");
  }

  // Stats::Store
  void addSink(Sink& sink) override { timer_sinks_.push_back(sink); }
  Counter& counter(const std::string& name) override { return counters_.get(name); }
  std::list<std::reference_wrapper<Counter>> counters() const override {
    return counters_.toList<Counter>();
  }

  void deliverHistogramToSinks(const std::string& name, uint64_t value) override {
    for (Sink& sink : timer_sinks_) {
      sink.onHistogramComplete(name, value);
    }
  }

  void deliverTimingToSinks(const std::string& name, std::chrono::milliseconds ms) {
    for (Sink& sink : timer_sinks_) {
      sink.onTimespanComplete(name, ms);
    }
  }

  Gauge& gauge(const std::string& name) override { return gauges_.get(name); }
  std::list<std::reference_wrapper<Gauge>> gauges() const override {
    return gauges_.toList<Gauge>();
  }
  Timer& timer(const std::string& name) override { return timers_.get(name); }

private:
  RawStatData& safeAlloc(const std::string& name) {
    RawStatData* data = alloc_.alloc(name);
    if (!data) {
      // If we run out of stat space from the allocator (which can happen if for example allocations
      // are coming from a fixed shared memory region, we need to deal with this case the best we
      // can.
      num_last_resort_stats_.inc();
      return last_resort_stat_data_;
    } else {
      return *data;
    }
  }

  std::list<std::reference_wrapper<Sink>> timer_sinks_;
  RawStatDataAllocator& alloc_;
  StatThreadLocalCache<CounterImpl> counters_;
  StatThreadLocalCache<GaugeImpl> gauges_;
  StatThreadLocalCache<TimerImpl> timers_;
  RawStatData last_resort_stat_data_;
  Counter& num_last_resort_stats_;
};

/**
 * Implementation of RawStatDataAllocator that just allocates a new structure in memory and returns
 * it.
 */
class HeapRawStatDataAllocator : public RawStatDataAllocator {
public:
  // RawStatDataAllocator
  RawStatData* alloc(const std::string& name) override;

private:
  std::list<std::unique_ptr<RawStatData>> raw_data_list_;
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
    stats_.emplace(name, std::unique_ptr<Impl>{new_stat});
    return *new_stat;
  }

  std::list<std::reference_wrapper<Base>> toList() const {
    std::list<std::reference_wrapper<Base>> list;
    for (auto& stat : stats_) {
      list.push_back(*stat.second);
    }

    return list;
  }

private:
  std::unordered_map<std::string, std::unique_ptr<Impl>> stats_;
  Allocator alloc_;
};

/**
 * Store implementation that is isolated from other stores.
 */
class IsolatedStoreImpl : public Store {
public:
  IsolatedStoreImpl()
      : counters_([this](const std::string& name)
                      -> CounterImpl* { return new CounterImpl(*alloc_.alloc(name)); }),
        gauges_([this](const std::string& name)
                    -> GaugeImpl* { return new GaugeImpl(*alloc_.alloc(name)); }),
        timers_([this](const std::string& name)
                    -> TimerImpl* { return new TimerImpl(name, *this); }) {}

  // Stats::Store
  void addSink(Sink&) override {}
  Counter& counter(const std::string& name) override { return counters_.get(name); }
  void deliverHistogramToSinks(const std::string&, uint64_t) override {}
  void deliverTimingToSinks(const std::string&, std::chrono::milliseconds) override {}
  std::list<std::reference_wrapper<Counter>> counters() const override {
    return counters_.toList();
  }
  Gauge& gauge(const std::string& name) override { return gauges_.get(name); }
  std::list<std::reference_wrapper<Gauge>> gauges() const override { return gauges_.toList(); }
  Timer& timer(const std::string& name) override { return timers_.get(name); }

private:
  HeapRawStatDataAllocator alloc_;
  IsolatedStatsCache<Counter, CounterImpl> counters_;
  IsolatedStatsCache<Gauge, GaugeImpl> gauges_;
  IsolatedStatsCache<Timer, TimerImpl> timers_;
};

} // Stats
