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
#include "envoy/server/options.h"
#include "envoy/stats/stats.h"

#include "common/common/assert.h"

namespace Envoy {
namespace Stats {

/**
 * Common stats utility routines.
 */
class Utility {
public:
  // ':' is a reserved char in statsd. Do a character replacement to avoid costly inline
  // translations later.
  static std::string sanitizeStatsName(const std::string& name);
};

/**
 * This structure is the backing memory for both CounterImpl and GaugeImpl. It is designed so that
 * it can be allocated from shared memory if needed.
 *
 * @note Due to name_ being variable size, sizeof(RawStatData) probably isn't useful.  Use
 * RawStatData::size() instead.
 */
struct RawStatData {
  struct Flags {
    static const uint8_t Used = 0x1;
  };

  /**
   * Due to the flexible-array-length of name_, c-style allocation
   * and initialization are neccessary.
   */
  RawStatData() = delete;
  ~RawStatData() = delete;

  /**
   * Configure static settings.  This MUST be called
   * before any other static or instance methods.
   */
  static void configure(Server::Options& options);

  /**
   * Allow tests to re-configure this value after it has been set.
   * This is unsafe in a non-test context.
   */
  static void configureForTestsOnly(Server::Options& options);

  /**
   * Returns the maximum length of the name of a stat.  This length
   * does not include a trailing NULL-terminator.
   */
  static size_t maxNameLength() {
    return initializeAndGetMutableMaxNameLength(DEFAULT_MAX_NAME_SIZE);
  }

  /**
   * size in bytes of name_
   */
  static size_t nameSize() { return maxNameLength() + 1; }

  /**
   * Returns the size of this struct, accounting for the length of name_
   * and padding for alignment.
   */
  static size_t size();

  /**
   * Initializes this object to have the specified name,
   * a refcount of 1, and all other values zero.
   */
  void initialize(const std::string& name);

  /**
   * Returns true if object is in use.
   */
  bool initialized() { return name_[0] != '\0'; }

  /**
   * Returns true if this matches name, truncated to maxNameLength().
   */
  bool matches(const std::string& name);

  std::atomic<uint64_t> value_;
  std::atomic<uint64_t> pending_increment_;
  std::atomic<uint16_t> flags_;
  std::atomic<uint16_t> ref_count_;
  std::atomic<uint32_t> unused_;
  char name_[];

private:
  static const size_t DEFAULT_MAX_NAME_SIZE = 127;

  static size_t& initializeAndGetMutableMaxNameLength(size_t configured_size);
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
 * Implementation of the Metric interface. Virtual inheritance is used because the interfaces that
 * will inherit from Metric will have other base classes that will also inherit from Metric.
 */
class MetricImpl : public virtual Metric {
public:
  MetricImpl(const std::string& name) : name_(name) {}

  const std::string& name() const override { return name_; }

private:
  const std::string name_;
};

/**
 * Counter implementation that wraps a RawStatData.
 */
class CounterImpl : public Counter, public MetricImpl {
public:
  CounterImpl(RawStatData& data, RawStatDataAllocator& alloc)
      : MetricImpl(data.name_), data_(data), alloc_(alloc) {}
  ~CounterImpl() { alloc_.free(data_); }

  // Stats::Counter
  void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.pending_increment_ += amount;
    data_.flags_ |= RawStatData::Flags::Used;
  }

  void inc() override { add(1); }
  uint64_t latch() override { return data_.pending_increment_.exchange(0); }
  void reset() override { data_.value_ = 0; }
  bool used() const override { return data_.flags_ & RawStatData::Flags::Used; }
  uint64_t value() const override { return data_.value_; }

private:
  RawStatData& data_;
  RawStatDataAllocator& alloc_;
};

/**
 * Gauge implementation that wraps a RawStatData.
 */
class GaugeImpl : public Gauge, public MetricImpl {
public:
  GaugeImpl(RawStatData& data, RawStatDataAllocator& alloc)
      : MetricImpl(data.name_), data_(data), alloc_(alloc) {}
  ~GaugeImpl() { alloc_.free(data_); }

  // Stats::Gauge
  virtual void add(uint64_t amount) override {
    data_.value_ += amount;
    data_.flags_ |= RawStatData::Flags::Used;
  }
  virtual void dec() override { sub(1); }
  virtual void inc() override { add(1); }
  virtual void set(uint64_t value) override {
    data_.value_ = value;
    data_.flags_ |= RawStatData::Flags::Used;
  }
  virtual void sub(uint64_t amount) override {
    ASSERT(data_.value_ >= amount);
    ASSERT(used());
    data_.value_ -= amount;
  }
  virtual uint64_t value() const override { return data_.value_; }
  bool used() const override { return data_.flags_ & RawStatData::Flags::Used; }

private:
  RawStatData& data_;
  RawStatDataAllocator& alloc_;
};

/**
 * Histogram implementation for the heap.
 */
class HistogramImpl : public Histogram, public MetricImpl {
public:
  HistogramImpl(const std::string& name, Store& parent) : MetricImpl(name), parent_(parent) {}

  // Stats::Histogram
  void recordValue(uint64_t value) override { parent_.deliverHistogramToSinks(*this, value); }

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
      : counters_([this](const std::string& name) -> CounterImpl* {
          return new CounterImpl(*alloc_.alloc(name), alloc_);
        }),
        gauges_([this](const std::string& name) -> GaugeImpl* {
          return new GaugeImpl(*alloc_.alloc(name), alloc_);
        }),
        histograms_([this](const std::string& name) -> HistogramImpl* {
          return new HistogramImpl(name, *this);
        }) {}

  // Stats::Scope
  Counter& counter(const std::string& name) override { return counters_.get(name); }
  ScopePtr createScope(const std::string& name) override {
    return ScopePtr{new ScopeImpl(*this, name)};
  }
  void deliverHistogramToSinks(const Histogram&, uint64_t) override {}
  Gauge& gauge(const std::string& name) override { return gauges_.get(name); }
  Histogram& histogram(const std::string& name) override {
    Histogram& histogram = histograms_.get(name);
    return histogram;
  }

  // Stats::Store
  std::list<CounterSharedPtr> counters() const override { return counters_.toList(); }
  std::list<GaugeSharedPtr> gauges() const override { return gauges_.toList(); }

private:
  struct ScopeImpl : public Scope {
    ScopeImpl(IsolatedStoreImpl& parent, const std::string& prefix)
        : parent_(parent), prefix_(Utility::sanitizeStatsName(prefix)) {}

    // Stats::Scope
    ScopePtr createScope(const std::string& name) override {
      return ScopePtr{new ScopeImpl(parent_, prefix_ + name)};
    }
    void deliverHistogramToSinks(const Histogram&, uint64_t) override {}
    Counter& counter(const std::string& name) override { return parent_.counter(prefix_ + name); }
    Gauge& gauge(const std::string& name) override { return parent_.gauge(prefix_ + name); }
    Histogram& histogram(const std::string& name) override {
      return parent_.histogram(prefix_ + name);
    }

    IsolatedStoreImpl& parent_;
    const std::string prefix_;
  };

  HeapRawStatDataAllocator alloc_;
  IsolatedStatsCache<Counter, CounterImpl> counters_;
  IsolatedStatsCache<Gauge, GaugeImpl> gauges_;
  IsolatedStatsCache<Histogram, HistogramImpl> histograms_;
};

} // namespace Stats
} // namespace Envoy
