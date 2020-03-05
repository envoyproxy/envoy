#pragma once

#include <cstdint>
#include <string>

#include "envoy/stats/histogram.h"
#include "envoy/stats/stats.h"
#include "envoy/stats/store.h"

#include "common/common/non_copyable.h"
#include "common/stats/metric_impl.h"

#include "circllhist.h"

namespace Envoy {
namespace Stats {

/**
 * Implementation of HistogramStatistics for circllhist.
 */
class HistogramStatisticsImpl : public HistogramStatistics, NonCopyable {
public:
  HistogramStatisticsImpl() : computed_quantiles_(supportedQuantiles().size(), 0.0) {}
  /**
   * HistogramStatisticsImpl object is constructed using the passed in histogram.
   * @param histogram_ptr pointer to the histogram for which stats will be calculated. This pointer
   * will not be retained.
   */
  HistogramStatisticsImpl(const histogram_t* histogram_ptr);

  void refresh(const histogram_t* new_histogram_ptr);

  // HistogramStatistics
  std::string quantileSummary() const override;
  std::string bucketSummary() const override;
  const std::vector<double>& supportedQuantiles() const override;
  const std::vector<double>& computedQuantiles() const override { return computed_quantiles_; }
  const std::vector<double>& supportedBuckets() const override;
  const std::vector<uint64_t>& computedBuckets() const override { return computed_buckets_; }
  uint64_t sampleCount() const override { return sample_count_; }
  double sampleSum() const override { return sample_sum_; }

private:
  std::vector<double> computed_quantiles_;
  std::vector<uint64_t> computed_buckets_;
  uint64_t sample_count_;
  double sample_sum_;
};

class HistogramImplHelper : public MetricImpl<Histogram> {
public:
  HistogramImplHelper(StatName name, StatName tag_extracted_name,
                      const StatNameTagVector& stat_name_tags, SymbolTable& symbol_table)
      : MetricImpl<Histogram>(name, tag_extracted_name, stat_name_tags, symbol_table) {}
  HistogramImplHelper(SymbolTable& symbol_table) : MetricImpl<Histogram>(symbol_table) {}

  // RefcountInterface
  void incRefCount() override { refcount_helper_.incRefCount(); }
  bool decRefCount() override { return refcount_helper_.decRefCount(); }
  uint32_t use_count() const override { return refcount_helper_.use_count(); }

private:
  RefcountHelper refcount_helper_;
};

/**
 * Histogram implementation for the heap.
 */
class HistogramImpl : public HistogramImplHelper {
public:
  HistogramImpl(StatName name, Unit unit, Store& parent, StatName tag_extracted_name,
                const StatNameTagVector& stat_name_tags)
      : HistogramImplHelper(name, tag_extracted_name, stat_name_tags, parent.symbolTable()),
        unit_(unit), parent_(parent) {}
  ~HistogramImpl() override {
    // We must explicitly free the StatName here in order to supply the
    // SymbolTable reference. An RAII alternative would be to store a
    // reference to the SymbolTable in MetricImpl, which would cost 8 bytes
    // per stat.
    MetricImpl::clear(symbolTable());
  }

  // Stats::Histogram
  Unit unit() const override { return unit_; };
  void recordValue(uint64_t value) override { parent_.deliverHistogramToSinks(*this, value); }

  bool used() const override { return true; }
  SymbolTable& symbolTable() override { return parent_.symbolTable(); }

private:
  Unit unit_;

  // This is used for delivering the histogram data to sinks.
  Store& parent_;
};

/**
 * Null histogram implementation.
 * No-ops on all calls and requires no underlying metric or data.
 */
class NullHistogramImpl : public HistogramImplHelper {
public:
  explicit NullHistogramImpl(SymbolTable& symbol_table)
      : HistogramImplHelper(symbol_table), symbol_table_(symbol_table) {}
  ~NullHistogramImpl() override { MetricImpl::clear(symbol_table_); }

  bool used() const override { return false; }
  SymbolTable& symbolTable() override { return symbol_table_; }

  Unit unit() const override { return Unit::Null; };
  void recordValue(uint64_t) override {}

private:
  SymbolTable& symbol_table_;
};

} // namespace Stats
} // namespace Envoy
