#include "common/stats/histogram_impl.h"

#include <algorithm>
#include <string>

#include "common/common/utility.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Stats {

HistogramStatisticsImpl::HistogramStatisticsImpl(const histogram_t* histogram_ptr)
    : computed_quantiles_(supportedQuantiles().size(), 0.0) {
  hist_approx_quantile(histogram_ptr, supportedQuantiles().data(), supportedQuantiles().size(),
                       computed_quantiles_.data());
}

const std::vector<double>& HistogramStatisticsImpl::supportedQuantiles() const {
  static const std::vector<double> supported_quantiles = {0,    0.25, 0.5,   0.75,  0.90,
                                                          0.95, 0.99, 0.995, 0.999, 1};
  return supported_quantiles;
}

std::string HistogramStatisticsImpl::summary() const {
  std::vector<std::string> summary;
  const std::vector<double>& supported_quantiles_ref = supportedQuantiles();
  summary.reserve(supported_quantiles_ref.size());
  for (size_t i = 0; i < supported_quantiles_ref.size(); ++i) {
    summary.push_back(
        fmt::format("P{}: {}", 100 * supported_quantiles_ref[i], computed_quantiles_[i]));
  }
  return absl::StrJoin(summary, ", ");
}

/**
 * Clears the old computed values and refreshes it with values computed from passed histogram.
 */
void HistogramStatisticsImpl::refresh(const histogram_t* new_histogram_ptr) {
  std::fill(computed_quantiles_.begin(), computed_quantiles_.end(), 0.0);
  ASSERT(supportedQuantiles().size() == computed_quantiles_.size());
  hist_approx_quantile(new_histogram_ptr, supportedQuantiles().data(), supportedQuantiles().size(),
                       computed_quantiles_.data());
}

} // namespace Stats
} // namespace Envoy
