#include "common/stats/stats_matcher_impl.h"

#include <regex>
#include <string>

#include "envoy/config/metrics/v3/stats.pb.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Stats {

// TODO(ambuc): Refactor this into common/matchers.cc, since StatsMatcher is really just a thin
// wrapper around what might be called a StringMatcherList.
StatsMatcherImpl::StatsMatcherImpl(const envoy::config::metrics::v3::StatsConfig& config) {
  switch (config.stats_matcher().stats_matcher_case()) {
  case envoy::config::metrics::v3::StatsMatcher::StatsMatcherCase::kRejectAll:
    // In this scenario, there are no matchers to store.
    is_inclusive_ = !config.stats_matcher().reject_all();
    break;
  case envoy::config::metrics::v3::StatsMatcher::StatsMatcherCase::kInclusionList:
    // If we have an inclusion list, we are being default-exclusive.
    for (const auto& stats_matcher : config.stats_matcher().inclusion_list().patterns()) {
      matchers_.push_back(Matchers::StringMatcherImpl(stats_matcher));
    }
    is_inclusive_ = false;
    break;
  case envoy::config::metrics::v3::StatsMatcher::StatsMatcherCase::kExclusionList:
    // If we have an exclusion list, we are being default-inclusive.
    for (const auto& stats_matcher : config.stats_matcher().exclusion_list().patterns()) {
      matchers_.push_back(Matchers::StringMatcherImpl(stats_matcher));
    }
    FALLTHRU;
  default:
    // No matcher was supplied, so we default to inclusion.
    is_inclusive_ = true;
    break;
  }
}

bool StatsMatcherImpl::rejects(const std::string& name) const {
  //
  //  is_inclusive_ | match | return
  // ---------------+-------+--------
  //        T       |   T   |   T     Default-inclusive and matching an (exclusion) matcher, deny.
  //        T       |   F   |   F     Otherwise, allow.
  //        F       |   T   |   F     Default-exclusive and matching an (inclusion) matcher, allow.
  //        F       |   F   |   T     Otherwise, deny.
  //
  // This is an XNOR, which can be evaluated by checking for equality.

  return (is_inclusive_ == std::any_of(matchers_.begin(), matchers_.end(),
                                       [&name](auto& matcher) { return matcher.match(name); }));
}

} // namespace Stats
} // namespace Envoy
