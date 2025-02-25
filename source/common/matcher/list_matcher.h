#pragma once

#include "envoy/matcher/matcher.h"

#include "source/common/matcher/field_matcher.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Matcher {

template <class DataType> class ListMatcherReentrant;
/**
 * A match tree that iterates over a list of matchers to find the first one that matches. If one
 * does, the MatchResult will be the one specified by the individual matcher.
 */
template <class DataType> class ListMatcher : public MatchTree<DataType> {
public:
  explicit ListMatcher(absl::optional<OnMatch<DataType>> on_no_match) : on_no_match_(on_no_match) {}

  typename MatchTree<DataType>::MatchResult match(const DataType& matching_data) override {
    return match(matching_data, matchers_, on_no_match_);
  }

  void addMatcher(FieldMatcherPtr<DataType>&& matcher, OnMatch<DataType> action) {
    matchers_.push_back({std::move(matcher), std::move(action)});
  }

  // Static helper with specifiable starting index.
  // Static helper with specifiable starting index.
  static typename MatchTree<DataType>::MatchResult
  match(const DataType& matching_data,
        std::vector<std::pair<FieldMatcherPtr<DataType>, OnMatch<DataType>>>& matchers,
        absl::optional<OnMatch<DataType>> on_no_match, int starting_index = 0) {
    for (size_t i = starting_index; i < matchers.size(); ++i) {
      const auto& matcher = matchers.at(i);
      const auto maybe_match = matcher.first->match(matching_data);

      // One of the matchers don't have enough information, bail on evaluating the match.
      if (maybe_match.match_state_ == MatchState::UnableToMatch) {
        return {MatchState::UnableToMatch, {}};
      }

      if (maybe_match.result()) {
        // If not at the end of the list, support the option to re-enter from the next index.
        return {MatchState::MatchComplete, matcher.second,
                std::make_unique<ListMatcherReentrant<DataType>>(&matchers, on_no_match, i + 1)};
      }
    }
    return {MatchState::MatchComplete, on_no_match};
  }

private:
  absl::optional<OnMatch<DataType>> on_no_match_;
  std::vector<std::pair<FieldMatcherPtr<DataType>, OnMatch<DataType>>> matchers_;
};

// Referential class to allow for re-entry into a ListMatcher after an initial match.
template <class DataType> class ListMatcherReentrant : public MatchTree<DataType> {
public:
  explicit ListMatcherReentrant(
      std::vector<std::pair<FieldMatcherPtr<DataType>, OnMatch<DataType>>>* parent_matchers,
      absl::optional<OnMatch<DataType>> on_no_match, int starting_index)
      : parent_matchers_(parent_matchers), on_no_match_(on_no_match),
        starting_index_(starting_index) {}

  typename MatchTree<DataType>::MatchResult match(const DataType& matching_data) override {
    return ListMatcher<DataType>::match(matching_data, *parent_matchers_, on_no_match_,
                                        starting_index_);
  }

private:
  std::vector<std::pair<FieldMatcherPtr<DataType>, OnMatch<DataType>>>* parent_matchers_;
  absl::optional<OnMatch<DataType>> on_no_match_;
  int starting_index_;
};

} // namespace Matcher
} // namespace Envoy
