#include "common/stats/histogram_impl.h"

#include <algorithm>
#include <string>

#include "common/common/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

HistogramStatisticsImpl::HistogramStatisticsImpl(const histogram_t* histogram_ptr)
    : computed_quantiles_(HistogramStatisticsImpl::supportedQuantiles().size(), 0.0) {
  hist_approx_quantile(histogram_ptr, supportedQuantiles().data(),
                       HistogramStatisticsImpl::supportedQuantiles().size(),
                       computed_quantiles_.data());

  sample_count_ = hist_sample_count(histogram_ptr);
  sample_sum_ = hist_approx_sum(histogram_ptr);

  const std::vector<double>& supported_buckets = supportedBuckets();
  computed_buckets_.reserve(supported_buckets.size());
  for (const auto bucket : supported_buckets) {
    computed_buckets_.emplace_back(hist_approx_count_below(histogram_ptr, bucket));
  }
}

const std::vector<double>& HistogramStatisticsImpl::supportedQuantiles() const {
  CONSTRUCT_ON_FIRST_USE(std::vector<double>,
                         {0, 0.25, 0.5, 0.75, 0.90, 0.95, 0.99, 0.995, 0.999, 1});
}

const std::vector<double>& HistogramStatisticsImpl::supportedBuckets() const {
  CONSTRUCT_ON_FIRST_USE(std::vector<double>,
                         {0.5, 1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 30000,
                          60000, 300000, 600000, 1800000, 3600000});
}

std::string HistogramStatisticsImpl::quantileSummary() const {
  std::vector<std::string> summary;
  const std::vector<double>& supported_quantiles = supportedQuantiles();
  summary.reserve(supported_quantiles.size());
  for (size_t i = 0; i < supported_quantiles.size(); ++i) {
    summary.push_back(
        fmt::format("P{:g}: {:g}", 100 * supported_quantiles[i], computed_quantiles_[i]));
  }
  return absl::StrJoin(summary, ", ");
}

std::string HistogramStatisticsImpl::bucketSummary() const {
  std::vector<std::string> bucket_summary;
  const std::vector<double>& supported_buckets = supportedBuckets();
  bucket_summary.reserve(supported_buckets.size());
  for (size_t i = 0; i < supported_buckets.size(); ++i) {
    bucket_summary.push_back(fmt::format("B{:g}: {}", supported_buckets[i], computed_buckets_[i]));
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

  ASSERT(supportedBuckets().size() == computed_buckets_.size());
  computed_buckets_.clear();
  const std::vector<double>& supported_buckets = supportedBuckets();
  computed_buckets_.reserve(supported_buckets.size());
  for (const auto bucket : supported_buckets) {
    computed_buckets_.emplace_back(hist_approx_count_below(new_histogram_ptr, bucket));
  }
}

} // namespace Stats
} // namespace Envoy
