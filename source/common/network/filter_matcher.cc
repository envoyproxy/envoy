#include "common/network/filter_matcher.h"

#include "envoy/network/filter.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Network {

ListenerFilterMatcherPtr ListenerFilterMatcherBuilder::buildListenerFilterMatcher(
    const envoy::config::listener::v3::ListenerFilterChainMatchPredicate& match_config) {
  switch (match_config.rule_case()) {
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kAnyMatch:
    return std::make_unique<ListenerFilterAnyMatcher>();
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kNotMatch: {
    return std::make_unique<ListenerFilterNotMatcher>(match_config.not_match());
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kAndMatch: {
    return std::make_unique<ListenerFilterAndMatcher>(match_config.and_match().rules());
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::kOrMatch: {
    return std::make_unique<ListenerFilterOrMatcher>(match_config.or_match().rules());
  }
  case envoy::config::listener::v3::ListenerFilterChainMatchPredicate::RuleCase::
      kDestinationPortRange: {
    return std::make_unique<ListenerFilterDstPortMatcher>(match_config.destination_port_range());
  }
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

ListenerFilterSetLogicMatcher::ListenerFilterSetLogicMatcher(
    absl::Span<const ::envoy::config::listener::v3::ListenerFilterChainMatchPredicate* const>
        predicates)
    : sub_matchers_(predicates.length()) {
  std::transform(predicates.begin(), predicates.end(), sub_matchers_.begin(), [](const auto* pred) {
    return ListenerFilterMatcherBuilder::buildListenerFilterMatcher(*pred);
  });
}

bool ListenerFilterOrMatcher::matches(ListenerFilterCallbacks& cb) const {
  return std::any_of(sub_matchers_.begin(), sub_matchers_.end(),
                     [&cb](const auto& matcher) { return matcher->matches(cb); });
}

bool ListenerFilterAndMatcher::matches(ListenerFilterCallbacks& cb) const {
  return std::all_of(sub_matchers_.begin(), sub_matchers_.end(),
                     [&cb](const auto& matcher) { return matcher->matches(cb); });
}

} // namespace Network
} // namespace Envoy