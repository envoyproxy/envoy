#include "common/stats/stats_matcher_impl.h"

#include <regex>
#include <string>

#include "common/common/utility.h"

namespace Envoy {
namespace Stats {

StatsMatcherImpl::StatsMatcherImpl(const envoy::config::metrics::v2::StatsConfig& config) {
  switch (config.stats_matcher().stats_matcher_case()) {
  case envoy::config::metrics::v2::StatsMatcher::kExclusionList:
    for (const auto& stats_matcher : config.stats_matcher().exclusion_list().patterns()) {
      matchers_.push_back(Matchers::StringMatcher(stats_matcher));
    }
    break;
  case envoy::config::metrics::v2::StatsMatcher::kInclusionList:
    default_inclusive_ = false;
    for (const auto& stats_matcher : config.stats_matcher().inclusion_list().patterns()) {
      matchers_.push_back(Matchers::StringMatcher(stats_matcher));
    }
    break;
  default:
    // No matcher was supplied, so we default to allow all stat names.
    break;
  }
}

bool StatsMatcherImpl::rejects(const std::string& name) const {
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
