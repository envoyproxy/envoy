#include "source/common/stats/stats_matcher_impl.h"

#include <regex>
#include <string>

#include "envoy/config/metrics/v3/stats.pb.h"

#include "source/common/common/utility.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Stats {

// TODO(ambuc): Refactor this into common/matchers.cc, since StatsMatcher is really just a thin
// wrapper around what might be called a StringMatcherList.
StatsMatcherImpl::StatsMatcherImpl(const envoy::config::metrics::v3::StatsConfig& config,
                                   SymbolTable& symbol_table)
    : symbol_table_(symbol_table), stat_name_pool_(std::make_unique<StatNamePool>(symbol_table)) {

  switch (config.stats_matcher().stats_matcher_case()) {
  case envoy::config::metrics::v3::StatsMatcher::StatsMatcherCase::kRejectAll:
    // In this scenario, there are no matchers to store.
    is_inclusive_ = !config.stats_matcher().reject_all();
    break;
  case envoy::config::metrics::v3::StatsMatcher::StatsMatcherCase::kInclusionList:
    // If we have an inclusion list, we are being default-exclusive.
    for (const auto& stats_matcher : config.stats_matcher().inclusion_list().patterns()) {
      matchers_.push_back(Matchers::StringMatcherImpl(stats_matcher));
      optimizeLastMatcher();
    }
    is_inclusive_ = false;
    break;
  case envoy::config::metrics::v3::StatsMatcher::StatsMatcherCase::kExclusionList:
    // If we have an exclusion list, we are being default-inclusive.
    for (const auto& stats_matcher : config.stats_matcher().exclusion_list().patterns()) {
      matchers_.push_back(Matchers::StringMatcherImpl(stats_matcher));
      optimizeLastMatcher();
    }
    FALLTHRU;
  default:
    // No matcher was supplied, so we default to inclusion.
    is_inclusive_ = true;
    break;
  }
}

void StatsMatcherImpl::optimizeLastMatcher() {
  std::string prefix;
  if (matchers_.back().getCaseSensitivePrefixMatch(prefix) && absl::EndsWith(prefix, ".") &&
      prefix.size() > 1) {
    prefixes_.push_back(stat_name_pool_->add(prefix.substr(0, prefix.size() - 1)));
    matchers_.pop_back();
  }
}

bool StatsMatcherImpl::rejects(StatName stat_name) const {
  if (rejectsAll()) {
    return true;
  }

  bool match = fastRejectMatch(stat_name) || slowRejectMatch(stat_name);

  //  is_inclusive_ | match | return
  // ---------------+-------+--------
  //        T       |   T   |   T     Default-inclusive and matching an (exclusion) matcher, deny.
  //        T       |   F   |   F     Otherwise, allow.
  //        F       |   T   |   F     Default-exclusive and matching an (inclusion) matcher, allow.
  //        F       |   F   |   T     Otherwise, deny.
  //
  // This is an XNOR, which can be evaluated by checking for equality.
  return is_inclusive_ == match;
}

bool StatsMatcherImpl::fastRejects(StatName stat_name) const {
  if (rejectsAll()) {
    return true;
  }

  // We can short-circuit the slow matchers only if they are empty, or if
  // we are in inclusive-mode and we find a match.
  if (is_inclusive_ || matchers_.empty()) {
    return fastRejectMatch(stat_name) == is_inclusive_;
  }

  // If there are both prefix and string matchers, and we are in exclusive
  // mode, then it isn't helpful to run the prefix matchers early, so
  // we return false. This forces the caller to eventually then call
  // slowRejects(), where we'll handle this case below.
  return false;
}

bool StatsMatcherImpl::fastRejectMatch(StatName stat_name) const {
  return std::any_of(prefixes_.begin(), prefixes_.end(),
                     [stat_name](StatName prefix) { return stat_name.startsWith(prefix); });
}

bool StatsMatcherImpl::slowRejects(StatName stat_name) const {
  bool match = slowRejectMatch(stat_name);

  //  is_inclusive_ | match | return
  // ---------------+-------+--------
  //        T       |   T   |   T     Default-inclusive and matching an (exclusion) matcher, deny.
  //        T       |   F   |   F     Otherwise, allow.
  //        F       |   T   |   F     Default-exclusive and matching an (inclusion) matcher, allow.
  //        F       |   F   |   T     Otherwise, deny.
  //
  // This is an XNOR, which can be evaluated by checking for equality.

  if (is_inclusive_ || match || prefixes_.empty()) {
    return match == is_inclusive_;
  }

  // In exclusive-mode with both prefixes and matches, we must
  // skip the fast-path prefix since we invert the results, so we can't
  // definitively declare a rejection. So we must check them now.
  return !fastRejectMatch(stat_name);
}

bool StatsMatcherImpl::slowRejectMatch(StatName stat_name) const {
  if (matchers_.empty()) {
    return false;
  }
  std::string name = symbol_table_->toString(stat_name);
  return std::any_of(matchers_.begin(), matchers_.end(),
                     [&name](auto& matcher) { return matcher.match(name); });
}

} // namespace Stats
} // namespace Envoy
