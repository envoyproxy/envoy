#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "envoy/common/optref.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

// Shared metrics registry for dynamic module extensions. It gives every extension the same superior
// metric set of counters, gauges, and histograms, each with optional labels, so the id-to-handle
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
        : name_(name), label_names_(label_names) {}
    const Stats::StatNameVec& labelNames() const { return label_names_; }
    void add(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::counterFromElements(scope, {name_}, tags).add(amount);
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
        : name_(name), label_names_(label_names), import_mode_(import_mode) {}
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
        : name_(name), label_names_(label_names), unit_(unit) {}
    const Stats::StatNameVec& labelNames() const { return label_names_; }
    void recordValue(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                     uint64_t value) const {
      ASSERT(tags.has_value());
      Stats::Utility::histogramFromElements(scope, {name_}, unit_, tags).recordValue(value);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
    Stats::Histogram::Unit unit_;
  };

  Stats::Scope& scope() { return scope_; }
  Stats::StatNamePool& statNamePool() { return stat_name_pool_; }

  // The ABI uses 1-based ids so 0 can mean unset. Storage is 0-based, so an id maps to index id-1.
  size_t addCounter(CounterHandle&& counter) {
    counters_.push_back(std::move(counter));
    return counters_.size();
  }
  size_t addCounterVec(CounterVecHandle&& counter_vec) {
    counter_vecs_.push_back(std::move(counter_vec));
    return counter_vecs_.size();
  }
  size_t addGauge(GaugeHandle&& gauge) {
    gauges_.push_back(std::move(gauge));
    return gauges_.size();
  }
  size_t addGaugeVec(GaugeVecHandle&& gauge_vec) {
    gauge_vecs_.push_back(std::move(gauge_vec));
    return gauge_vecs_.size();
  }
  size_t addHistogram(HistogramHandle&& hist) {
    hists_.push_back(std::move(hist));
    return hists_.size();
  }
  size_t addHistogramVec(HistogramVecHandle&& hist_vec) {
    hist_vecs_.push_back(std::move(hist_vec));
    return hist_vecs_.size();
  }

  OptRef<const CounterHandle> getCounterById(size_t id) const { return byId(counters_, id); }
  OptRef<const CounterVecHandle> getCounterVecById(size_t id) const {
    return byId(counter_vecs_, id);
  }
  OptRef<const GaugeHandle> getGaugeById(size_t id) const { return byId(gauges_, id); }
  OptRef<const GaugeVecHandle> getGaugeVecById(size_t id) const { return byId(gauge_vecs_, id); }
  OptRef<const HistogramHandle> getHistogramById(size_t id) const { return byId(hists_, id); }
  OptRef<const HistogramVecHandle> getHistogramVecById(size_t id) const {
    return byId(hist_vecs_, id);
  }

private:
  template <class T> static OptRef<const T> byId(const std::vector<T>& store, size_t id) {
    if (id == 0 || id > store.size()) {
      return {};
    }
    return store[id - 1];
  }

  Stats::Scope& scope_;
  Stats::StatNamePool stat_name_pool_;
  std::vector<CounterHandle> counters_;
  std::vector<CounterVecHandle> counter_vecs_;
  std::vector<GaugeHandle> gauges_;
  std::vector<GaugeVecHandle> gauge_vecs_;
  std::vector<HistogramHandle> hists_;
  std::vector<HistogramVecHandle> hist_vecs_;
};

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
