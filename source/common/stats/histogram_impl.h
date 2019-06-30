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

/**
 * Histogram implementation for the heap.
 */
class HistogramImpl : public MetricImpl<Histogram> {
public:
  HistogramImpl(StatName name, Store& parent, const std::string& tag_extracted_name,
                const std::vector<Tag>& tags)
      : MetricImpl<Histogram>(tag_extracted_name, tags, parent.symbolTable()),
        name_(name, parent.symbolTable()), parent_(parent) {}
  ~HistogramImpl() {
    // We must explicitly free the StatName here using the SymbolTable reference
    // we access via parent_. A pure RAII alternative would be to use
    // StatNameManagedStorage rather than StatNameStorage, which will cost a total
    // of 16 bytes per stat, counting the extra SymbolTable& reference here,
    // plus the extra SymbolTable& reference in MetricImpl.
    name_.free(symbolTable());

    // We must explicitly free the StatName here in order to supply the
    // SymbolTable reference. An RAII alternative would be to store a
    // reference to the SymbolTable in MetricImpl, which would cost 8 bytes
    // per stat.
    MetricImpl::clear(symbolTable());
  }

  // Stats::Histogram
  void recordValue(uint64_t value) override { parent_.deliverHistogramToSinks(*this, value); }

  bool used() const override { return true; }
  StatName statName() const override { return name_.statName(); }
  SymbolTable& symbolTable() override { return parent_.symbolTable(); }

private:
  StatNameStorage name_;

  // This is used for delivering the histogram data to sinks.
  Store& parent_;
};

/**
 * Null histogram implementation.
 * No-ops on all calls and requires no underlying metric or data.
 */
class NullHistogramImpl : public MetricImpl<Histogram> {
public:
  explicit NullHistogramImpl(SymbolTable& symbol_table)
      : MetricImpl<Histogram>(symbol_table), symbol_table_(symbol_table), name_("", symbol_table_) {
  }
  ~NullHistogramImpl() { MetricImpl::clear(symbol_table_); }

  StatName statName() const override { return name_.statName(); }
  bool used() const override { return false; }
  SymbolTable& symbolTable() override { return symbol_table_; }

  void recordValue(uint64_t) override {}

private:
  SymbolTable& symbol_table_;
  StatNameManagedStorage name_;
};

} // namespace Stats
} // namespace Envoy
