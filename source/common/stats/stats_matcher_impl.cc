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
                                   SymbolTable& symbol_table,
                                   Server::Configuration::CommonFactoryContext& context)
    : symbol_table_(symbol_table), stat_name_pool_(std::make_unique<StatNamePool>(symbol_table)) {

  switch (config.stats_matcher().stats_matcher_case()) {
  case envoy::config::metrics::v3::StatsMatcher::StatsMatcherCase::kRejectAll:
    // In this scenario, there are no matchers to store.
    is_inclusive_ = !config.stats_matcher().reject_all();
    break;
  case envoy::config::metrics::v3::StatsMatcher::StatsMatcherCase::kInclusionList:
    // If we have an inclusion list, we are being default-exclusive.
    for (const auto& stats_matcher : config.stats_matcher().inclusion_list().patterns()) {
      matchers_.push_back(Matchers::StringMatcherImpl(stats_matcher, context));
      optimizeLastMatcher();
    }
    is_inclusive_ = false;
    break;
  case envoy::config::metrics::v3::StatsMatcher::StatsMatcherCase::kExclusionList:
    // If we have an exclusion list, we are being default-inclusive.
    for (const auto& stats_matcher : config.stats_matcher().exclusion_list().patterns()) {
      matchers_.push_back(Matchers::StringMatcherImpl(stats_matcher, context));
      optimizeLastMatcher();
    }
    FALLTHRU;
  default:
    // No matcher was supplied, so we default to inclusion.
    is_inclusive_ = true;
    break;
  }
}

// If the last string-matcher added is a case-sensitive prefix match, and the
// prefix ends in ".", then this drops that match and adds it to a list of
// prefixes. This is beneficial because token prefixes can be handled more
// efficiently as a StatName without requiring conversion to a string.
//
// In the future, other matcher patterns could be optimized in a similar way,
// such as:
//   * suffixes that begin with "."
//   * exact-matches
//   * substrings that begin and end with "."
//
// These are left unoptimized for the moment to keep the code-change simpler,
// and because we haven't observed an acute performance need to optimize those
// other patterns yet.
void StatsMatcherImpl::optimizeLastMatcher() {
  std::string prefix;
  if (matchers_.back().getCaseSensitivePrefixMatch(prefix) && absl::EndsWith(prefix, ".") &&
      prefix.size() > 1) {
    prefixes_.push_back(stat_name_pool_->add(prefix.substr(0, prefix.size() - 1)));
    matchers_.pop_back();
  }
}

StatsMatcher::FastResult StatsMatcherImpl::fastRejects(StatName stat_name) const {
  if (rejectsAll()) {
    return FastResult::Rejects;
  }
  bool matches = fastRejectMatch(stat_name);
  if ((is_inclusive_ || matchers_.empty()) && matches == is_inclusive_) {
    // We can short-circuit the slow matchers only if they are empty, or if
    // we are in inclusive-mode and we find a match.
    return FastResult::Rejects;
  } else if (matches) {
    return FastResult::Matches;
  }
  return FastResult::NoMatch;
}

bool StatsMatcherImpl::fastRejectMatch(StatName stat_name) const {
  return std::any_of(prefixes_.begin(), prefixes_.end(),
                     [stat_name](StatName prefix) { return stat_name.startsWith(prefix); });
}

bool StatsMatcherImpl::slowRejects(FastResult fast_result, StatName stat_name) const {
  // Skip slowRejectMatch if we already have a definitive answer from fastRejects.
  if (fast_result != FastResult::NoMatch) {
    return fast_result == FastResult::Rejects;
  }

  const bool match = slowRejectMatch(stat_name);

  //  is_inclusive_ | match | return
  // ---------------+-------+--------
  //        T       |   T   |   T   Default-inclusive and matching an (exclusion) matcher, deny.
  //        T       |   F   |   F   Otherwise, allow.
  //        F       |   T   |   F   Default-exclusive and matching an (inclusion) matcher, allow.
  //        F       |   F   |   T   Otherwise, deny.
  //
  // This is an XNOR, which can be evaluated by checking for equality.
  return match == is_inclusive_;
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
