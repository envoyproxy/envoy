#pragma once

#include "envoy/matcher/matcher.h"

#include "source/common/matcher/field_matcher.h"

namespace Envoy {
namespace Matcher {

/**
 * A match tree that iterates over a list of matchers to find the first one that matches. If one
 * does, the MatchResult will be the one specified by the individual matcher.
 */
template <class DataType> class ListMatcher : public MatchTree<DataType> {
public:
  explicit ListMatcher(absl::optional<OnMatch<DataType>> on_no_match) : on_no_match_(on_no_match) {}

  typename MatchTree<DataType>::MatchResult match(const DataType& matching_data) override {
    for (const auto& matcher : matchers_) {
      const auto maybe_match = matcher.first->match(matching_data);

      // One of the matchers don't have enough information, bail on evaluating the match.
      if (maybe_match.match_state_ == MatchState::UnableToMatch) {
        return {MatchState::UnableToMatch, {}};
      }

      if (maybe_match.result()) {
        return {MatchState::MatchComplete, matcher.second};
      }
    }

    return {MatchState::MatchComplete, on_no_match_};
  }

  void addMatcher(FieldMatcherPtr<DataType>&& matcher, OnMatch<DataType> action) {
    matchers_.push_back({std::move(matcher), std::move(action)});
  }

private:
  absl::optional<OnMatch<DataType>> on_no_match_;
  std::vector<std::pair<FieldMatcherPtr<DataType>, OnMatch<DataType>>> matchers_;
};

} // namespace Matcher
} // namespace Envoy
