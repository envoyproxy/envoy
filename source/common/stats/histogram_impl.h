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
 * Parent histogram implementation, used in IsolatedStoreImpl.
 */
class ParentHistogramImpl : public ParentHistogram, public MetricImpl {
public:
  ParentHistogramImpl(const std::string& name, Store& parent, std::string&& tag_extracted_name,
                      std::vector<Tag>&& tags);

  // Stats::Metric
  std::string name() const override { return name_; }
  const char* nameCStr() const override { return name_.c_str(); }
  bool used() const override { return used_; }

  // Stats::Histogram
  void recordValue(uint64_t value) override;

  // Stats::ParentHistogram
  void merge() override;
  const HistogramStatistics& intervalStatistics() const override { return interval_statistics_; }
  const HistogramStatistics& cumulativeStatistics() const override {
    return cumulative_statistics_;
  }

private:
  struct TrivialStatistics : public HistogramStatistics {
    // Stats::HistogramStatistics
    const std::vector<double>& supportedQuantiles() const override { return empty_doubles_; }
    const std::vector<double>& computedQuantiles() const override { return empty_doubles_; }
    const std::vector<double>& supportedBuckets() const override { return empty_doubles_; }
    const std::vector<uint64_t>& computedBuckets() const override { return empty_ints_; }
    uint64_t sampleCount() const override { return count_; }
    double sampleSum() const override { return sum_; }

    static const std::vector<double> empty_doubles_;
    static const std::vector<uint64_t> empty_ints_;
    uint64_t count_{}, sum_{};
  };

  Store& parent_;
  uint64_t count_{}, sum_{};
  TrivialStatistics interval_statistics_;
  TrivialStatistics cumulative_statistics_;
  bool used_{};
  const std::string name_;
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
  const char* nameCStr() const override { return ""; }
  const std::string& tagExtractedName() const override { CONSTRUCT_ON_FIRST_USE(std::string, ""); }
  const std::vector<Tag>& tags() const override { CONSTRUCT_ON_FIRST_USE(std::vector<Tag>, {}); }
  void recordValue(uint64_t) override {}
  bool used() const override { return false; }
};

} // namespace Stats
} // namespace Envoy
