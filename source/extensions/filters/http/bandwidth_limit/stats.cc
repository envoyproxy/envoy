#include "source/extensions/filters/http/bandwidth_limit/stats.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

BandwidthLimitStats generateStats(absl::string_view prefix, Stats::Scope& scope) {
  const std::string final_prefix = absl::StrCat(prefix, ".http_bandwidth_limit");
  return {ALL_BANDWIDTH_LIMIT_STATS(POOL_COUNTER_PREFIX(scope, final_prefix),
                                    POOL_GAUGE_PREFIX(scope, final_prefix),
                                    POOL_HISTOGRAM_PREFIX(scope, final_prefix))};
}

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
