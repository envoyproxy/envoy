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

  using MatchResult = typename MatchTree<DataType>::MatchResult;

  typename MatchTree<DataType>::MatchResult
  match(const DataType& matching_data,
        SkippedMatchCb<DataType> skipped_match_cb = nullptr) override {
    for (const auto& matcher : matchers_) {
      FieldMatchResult result = matcher.first->match(matching_data);

      // One of the matchers don't have enough information, bail on evaluating the match.
      if (result.match_state_ == MatchState::UnableToMatch) {
        return {MatchState::UnableToMatch, absl::nullopt};
      }
      if (!result.result()) {
        continue;
      }

      MatchResult processed_result = MatchTree<DataType>::handleRecursionAndSkips(
          matcher.second, matching_data, skipped_match_cb);
      // Continue to next matcher if the result is a no-match or is skipped.
      if (processed_result.match_state_ != MatchState::MatchComplete ||
          processed_result.on_match_.has_value()) {
        return processed_result;
      }
    }

    // Return on-no-match, after keep_matching and/or recursion handling.
    return MatchTree<DataType>::handleRecursionAndSkips(on_no_match_, matching_data,
                                                        skipped_match_cb);
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
