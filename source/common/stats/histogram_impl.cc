#include "source/common/stats/histogram_impl.h"

#include <algorithm>
#include <string>

#include "source/common/common/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

namespace {
const ConstSupportedBuckets default_buckets{};
}

HistogramStatisticsImpl::HistogramStatisticsImpl()
    : supported_buckets_(default_buckets), computed_quantiles_(supportedQuantiles().size(), 0.0) {}

HistogramStatisticsImpl::HistogramStatisticsImpl(const histogram_t* histogram_ptr,
                                                 Histogram::Unit unit,
                                                 ConstSupportedBuckets& supported_buckets)
    : supported_buckets_(supported_buckets),
      computed_quantiles_(HistogramStatisticsImpl::supportedQuantiles().size(), 0.0), unit_(unit) {
  refresh(histogram_ptr);
}

const std::vector<double>& HistogramStatisticsImpl::supportedQuantiles() const {
  CONSTRUCT_ON_FIRST_USE(std::vector<double>,
                         {0, 0.25, 0.5, 0.75, 0.90, 0.95, 0.99, 0.995, 0.999, 1});
}

std::vector<uint64_t> HistogramStatisticsImpl::computeDisjointBuckets() const {
  std::vector<uint64_t> buckets;
  buckets.reserve(computed_buckets_.size());
  uint64_t previous_computed_bucket = 0;
  for (uint64_t computed_bucket : computed_buckets_) {
    buckets.push_back(computed_bucket - previous_computed_bucket);
    previous_computed_bucket = computed_bucket;
  }
  return buckets;
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
  ConstSupportedBuckets& supported_buckets = supportedBuckets();
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
  // Convert to double once to avoid needing to cast it on every use. Use a double
  // to ensure the compiler doesn't try to convert the expression to integer math.
  constexpr double percent_scale = Histogram::PercentScale;

  std::fill(computed_quantiles_.begin(), computed_quantiles_.end(), 0.0);
  ASSERT(supportedQuantiles().size() == computed_quantiles_.size());
  hist_approx_quantile(new_histogram_ptr, supportedQuantiles().data(), supportedQuantiles().size(),
                       computed_quantiles_.data());
  if (unit_ == Histogram::Unit::Percent) {
    for (double& val : computed_quantiles_) {
      val /= percent_scale;
    }
  }

  sample_count_ = hist_sample_count(new_histogram_ptr);
  sample_sum_ = hist_approx_sum(new_histogram_ptr);
  if (unit_ == Histogram::Unit::Percent) {
    sample_sum_ /= percent_scale;
  }

  computed_buckets_.clear();
  ConstSupportedBuckets& supported_buckets = supportedBuckets();
  computed_buckets_.reserve(supported_buckets.size());
  for (auto bucket : supported_buckets) {
    if (unit_ == Histogram::Unit::Percent) {
      bucket *= percent_scale;
    }
    computed_buckets_.emplace_back(hist_approx_count_below(new_histogram_ptr, bucket));
  }

  out_of_bound_count_ = hist_approx_count_above(new_histogram_ptr, supported_buckets.back());
}

HistogramSettingsImpl::HistogramSettingsImpl(const envoy::config::metrics::v3::StatsConfig& config,
                                             Server::Configuration::CommonFactoryContext& context)
    : configs_([&config, &context]() {
        std::vector<Config> configs;
        for (const auto& matcher : config.histogram_bucket_settings()) {
          std::vector<double> buckets{matcher.buckets().begin(), matcher.buckets().end()};
          std::sort(buckets.begin(), buckets.end());
          configs.emplace_back(Matchers::StringMatcherImpl<envoy::type::matcher::v3::StringMatcher>(
                                   matcher.match(), context),
                               std::move(buckets));
        }

        return configs;
      }()) {}

const ConstSupportedBuckets& HistogramSettingsImpl::buckets(absl::string_view stat_name) const {
  for (const auto& config : configs_) {
    if (config.first.match(stat_name)) {
      return config.second;
    }
  }
  return defaultBuckets();
}

const ConstSupportedBuckets& HistogramSettingsImpl::defaultBuckets() {
  CONSTRUCT_ON_FIRST_USE(ConstSupportedBuckets,
                         {0.5, 1, 5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 30000,
                          60000, 300000, 600000, 1800000, 3600000});
}

} // namespace Stats
} // namespace Envoy
