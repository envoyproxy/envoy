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
  std::string summary() const override;
  const std::vector<double>& supportedQuantiles() const override;
  const std::vector<double>& computedQuantiles() const override { return computed_quantiles_; }

private:
  std::vector<double> computed_quantiles_;
};

/**
 * Histogram implementation for the heap.
 */
class HistogramImpl : public Histogram, public MetricImpl {
public:
  HistogramImpl(StatName name, Store& parent, const std::string& tag_extracted_name,
                const std::vector<Tag>& tags, StatDataAllocator& alloc)
      : MetricImpl(tag_extracted_name, tags, alloc.symbolTable()), name_(name), parent_(parent) {}

  // Stats::Histogram
  void recordValue(uint64_t value) override { parent_.deliverHistogramToSinks(*this, value); }

  bool used() const override { return true; }

private:
  StatName name_;

  // This is used for delivering the histogram data to sinks.
  Store& parent_;
};

/**
 * Null histogram implementation.
 * No-ops on all calls and requires no underlying metric or data.
 */
class NullHistogramImpl : public Histogram {
public:
  NullHistogramImpl() {}
  ~NullHistogramImpl() {}
  std::string name() const override { return ""; }
  StatName statName() const override { return StatName(); }
  std::string tagExtractedName() const override { return ""; }
  std::vector<Tag> tags() const override { return std::vector<Tag>(); }
  void recordValue(uint64_t) override {}
  bool used() const override { return false; }
};

} // namespace Stats
} // namespace Envoy
