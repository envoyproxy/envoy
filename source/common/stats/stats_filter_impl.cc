#include "common/stats/stats_filter_impl.h"

#include <regex>
#include <string>

#include "common/common/utility.h"

namespace Envoy {
namespace Stats {

StatsFilterImpl::StatsFilterImpl(const envoy::config::metrics::v2::StatsFilter& filter) {
  switch (filter.stats_filter_case()) {
  case envoy::config::metrics::v2::StatsFilter::kExclusionList:
    for (const auto& stats_filter : filter.exclusion_list().patterns()) {
      matchers_.push_back(Matchers::StringMatcher(stats_filter));
    }
    break;
  case envoy::config::metrics::v2::StatsFilter::kInclusionList:
    default_inclusive_ = false;
    for (const auto& stats_filter : filter.inclusion_list().patterns()) {
      matchers_.push_back(Matchers::StringMatcher(stats_filter));
    }
    break;
  default:
    // No filter was supplied, so we default to allow all stat names.
  }
}

bool StatsFilterImpl::rejects(const std::string& name) const {
  if (default_inclusive_) {
    // If we are in default-allow mode, matching any of the matchers is grounds for denial.
    return std::any_of(matchers_.begin(), matchers_.end(),
                       [&name](auto matcher) { return matcher.match(name); });
  } else {
    // On the other hand, if we are in default-deny mode, matching any of the matchers is
    // grounds for admission.
    return !std::any_of(matchers_.begin(), matchers_.end(),
                        [&name](auto matcher) { return matcher.match(name); });
  }
}

} // namespace Stats
} // namespace Envoy
