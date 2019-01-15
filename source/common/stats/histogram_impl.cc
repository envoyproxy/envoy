#include "common/stats/histogram_impl.h"

#include <algorithm>
#include <string>

#include "common/common/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

HistogramStatisticsImpl::HistogramStatisticsImpl(const histogram_t* histogram_ptr)
    : computed_quantiles_(supportedQuantiles().size(), 0.0),
      computed_buckets_(supportedBuckets().size(), 0.0) {
  hist_approx_quantile(histogram_ptr, supportedQuantiles().data(), supportedQuantiles().size(),
                       computed_quantiles_.data());

  sample_count_ = hist_sample_count(histogram_ptr);
  sample_sum_ = hist_approx_sum(histogram_ptr);

  const std::vector<double>& supported_buckets_ref = supportedBuckets();
  for (size_t i = 0; i < supported_buckets_ref.size(); ++i) {
    computed_buckets_[i] = hist_approx_count_below(histogram_ptr, supported_buckets_ref[i]);
  }
}

const std::vector<double>& HistogramStatisticsImpl::supportedQuantiles() const {
  static const std::vector<double> supported_quantiles = {0,    0.25, 0.5,   0.75,  0.90,
                                                          0.95, 0.99, 0.995, 0.999, 1};
  return supported_quantiles;
}

const std::vector<double>& HistogramStatisticsImpl::supportedBuckets() const {
  static const std::vector<double> supported_buckets = {0.005, 0.01, 0.025, 0.05, 0.1, 0.25,
                                                        0.5,   1.0,  2.5,   5,    10};
  return supported_buckets;
}

std::string HistogramStatisticsImpl::quantileSummary() const {
  std::vector<std::string> summary;
  const std::vector<double>& supported_quantiles_ref = supportedQuantiles();
  summary.reserve(supported_quantiles_ref.size());
  for (size_t i = 0; i < supported_quantiles_ref.size(); ++i) {
    summary.push_back(
        fmt::format("P{}: {}", 100 * supported_quantiles_ref[i], computed_quantiles_[i]));
  }
  return absl::StrJoin(summary, ", ");
}

std::string HistogramStatisticsImpl::bucketSummary() const {
  std::vector<std::string> bucket_summary;
  const std::vector<double>& supported_buckets_ref = supportedBuckets();
  bucket_summary.reserve(supported_buckets_ref.size());
  for (size_t i = 0; i < supported_buckets_ref.size(); ++i) {
    bucket_summary.push_back(
        fmt::format("B{}: {}", supported_buckets_ref[i], computed_buckets_[i]));
  }
  return absl::StrJoin(bucket_summary, ", ");
}

/**
 * Clears the old computed values and refreshes it with values computed from passed histogram.
 */
void HistogramStatisticsImpl::refresh(const histogram_t* new_histogram_ptr) {
  std::fill(computed_quantiles_.begin(), computed_quantiles_.end(), 0.0);
  ASSERT(supportedQuantiles().size() == computed_quantiles_.size());
  hist_approx_quantile(new_histogram_ptr, supportedQuantiles().data(), supportedQuantiles().size(),
                       computed_quantiles_.data());

  sample_count_ = hist_sample_count(new_histogram_ptr);
  sample_sum_ = hist_approx_sum(new_histogram_ptr);

  std::fill(computed_buckets_.begin(), computed_buckets_.end(), 0.0);
  ASSERT(supportedBuckets().size() == computed_buckets_.size());
  const std::vector<double>& supported_buckets_ref = supportedBuckets();
  for (size_t i = 0; i < supported_buckets_ref.size(); ++i) {
    computed_buckets_[i] = hist_approx_count_below(new_histogram_ptr, supported_buckets_ref[i]);
  }
}

} // namespace Stats
} // namespace Envoy
