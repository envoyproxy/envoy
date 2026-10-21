#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/common/assert.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"

#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

// Shared metrics registry for dynamic module extensions. It gives every extension the same metric
// set of counters, gauges, and histograms, each with optional labels, so the id-to-handle
// glue is written once instead of once per extension category. An extension composes one registry
// from its stats scope, defines metrics on the main thread during config load, and records them by
// the opaque 1-based id the ABI hands back.
//
// Labels use the tags-friendly element API so the tag values are attached at record time without
// any name re-parsing. A metric defined without labels caches the resolved core metric and records
// with no allocation.
class MetricRegistry {
public:
  explicit MetricRegistry(Stats::Scope& scope)
      : scope_(scope), stat_name_pool_(scope.symbolTable()) {}

  // Handle for a metric defined without labels. It caches the resolved core metric so recording
  // allocates nothing.
  class CounterHandle {
  public:
    explicit CounterHandle(Stats::Counter& counter) : counter_(counter) {}
    void add(uint64_t amount) const { counter_.add(amount); }

  private:
    Stats::Counter& counter_;
  };

  // Handle for a labeled counter. The tag values are resolved per record and attached through the
  // element API so the flattened name stays stable.
  class CounterVecHandle {
  public:
    CounterVecHandle(Stats::StatName name, Stats::StatNameVec label_names)
        : name_(name), label_names_(std::move(label_names)) {}
    const Stats::StatNameVec& labelNames() const { return label_names_; }
    void add(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::counterFromElements(scope, {name_}, tags).add(amount);
    }
    // Resolves one label-value tuple to its child counter. The reference is stable for the life of
    // the scope, so a module holds it and skips the per-call name build that `add` pays.
    Stats::Counter& resolve(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags) const {
      ASSERT(tags.has_value());
      return Stats::Utility::counterFromElements(scope, {name_}, tags);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
  };

  class GaugeHandle {
  public:
    explicit GaugeHandle(Stats::Gauge& gauge) : gauge_(gauge) {}
    void increase(uint64_t amount) const { gauge_.add(amount); }
    void decrease(uint64_t amount) const { gauge_.sub(amount); }
    void set(uint64_t amount) const { gauge_.set(amount); }

  private:
    Stats::Gauge& gauge_;
  };

  class GaugeVecHandle {
  public:
    GaugeVecHandle(Stats::StatName name, Stats::StatNameVec label_names,
                   Stats::Gauge::ImportMode import_mode)
        : name_(name), label_names_(std::move(label_names)), import_mode_(import_mode) {}
    const Stats::StatNameVec& labelNames() const { return label_names_; }
    void increase(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                  uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).add(amount);
    }
    void decrease(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                  uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).sub(amount);
    }
    void set(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).set(amount);
    }
    // Resolves one label-value tuple to its child gauge. The reference is stable for the life of
    // the scope, so a module holds it and skips the per-call name build the record methods pay.
    Stats::Gauge& resolve(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags) const {
      ASSERT(tags.has_value());
      return Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
    Stats::Gauge::ImportMode import_mode_;
  };

  class HistogramHandle {
  public:
    explicit HistogramHandle(Stats::Histogram& histogram) : histogram_(histogram) {}
    void recordValue(uint64_t value) const { histogram_.recordValue(value); }

  private:
    Stats::Histogram& histogram_;
  };

  class HistogramVecHandle {
  public:
    HistogramVecHandle(Stats::StatName name, Stats::StatNameVec label_names,
                       Stats::Histogram::Unit unit)
        : name_(name), label_names_(std::move(label_names)), unit_(unit) {}
    const Stats::StatNameVec& labelNames() const { return label_names_; }
    void recordValue(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                     uint64_t value) const {
      ASSERT(tags.has_value());
      Stats::Utility::histogramFromElements(scope, {name_}, unit_, tags).recordValue(value);
    }
    // Resolves one label-value tuple to its child histogram. The reference is stable for the life
    // of the scope, so a module holds it and skips the per-call name build that `recordValue` pays.
    Stats::Histogram& resolve(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags) const {
      ASSERT(tags.has_value());
      return Stats::Utility::histogramFromElements(scope, {name_}, unit_, tags);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
    Stats::Histogram::Unit unit_;
  };

  Stats::Scope& scope() { return scope_; }
  Stats::StatNamePool& statNamePool() { return stat_name_pool_; }

  // The ABI uses 1-based ids so 0 can mean unset. Storage is 0-based, so an id maps to index id-1.
  // Metrics of every type and form share the ids, so an id resolves only to the metric it was
  // returned for.
  size_t addCounter(CounterHandle&& counter) { return add(std::move(counter)); }
  size_t addCounterVec(CounterVecHandle&& counter_vec) { return add(std::move(counter_vec)); }
  size_t addGauge(GaugeHandle&& gauge) { return add(std::move(gauge)); }
  size_t addGaugeVec(GaugeVecHandle&& gauge_vec) { return add(std::move(gauge_vec)); }
  size_t addHistogram(HistogramHandle&& histogram) { return add(std::move(histogram)); }
  size_t addHistogramVec(HistogramVecHandle&& histogram_vec) {
    return add(std::move(histogram_vec));
  }

  OptRef<const CounterHandle> getCounterById(size_t id) const { return byId<CounterHandle>(id); }
  OptRef<const CounterVecHandle> getCounterVecById(size_t id) const {
    return byId<CounterVecHandle>(id);
  }
  OptRef<const GaugeHandle> getGaugeById(size_t id) const { return byId<GaugeHandle>(id); }
  OptRef<const GaugeVecHandle> getGaugeVecById(size_t id) const { return byId<GaugeVecHandle>(id); }
  OptRef<const HistogramHandle> getHistogramById(size_t id) const {
    return byId<HistogramHandle>(id);
  }
  OptRef<const HistogramVecHandle> getHistogramVecById(size_t id) const {
    return byId<HistogramVecHandle>(id);
  }

private:
  using Handle = absl::variant<CounterHandle, CounterVecHandle, GaugeHandle, GaugeVecHandle,
                               HistogramHandle, HistogramVecHandle>;

  template <class T> size_t add(T&& handle) {
    handles_.emplace_back(std::forward<T>(handle));
    return handles_.size();
  }

  template <class T> OptRef<const T> byId(size_t id) const {
    if (id == 0 || id > handles_.size()) {
      return {};
    }
    return makeOptRefFromPtr(absl::get_if<T>(&handles_[id - 1]));
  }

  Stats::Scope& scope_;
  Stats::StatNamePool stat_name_pool_;
  std::vector<Handle> handles_;
};

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
