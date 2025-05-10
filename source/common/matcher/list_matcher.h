#pragma once

#include "envoy/matcher/matcher.h"

#include "source/common/matcher/field_matcher.h"

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

  void addMatcher(FieldMatcherPtr<DataType>&& matcher, OnMatch<DataType> action) {
    matchers_.push_back({std::move(matcher), std::move(action)});
  }

  // Helper with specifiable starting index. Not part of the MatchTree interface.
  typename MatchTree<DataType>::MatchResult matchImpl(const DataType& matching_data,
                                                      int starting_index = 0) {
    // Start traversal from non-zero index during re-entry.
    for (size_t i = starting_index; i < matchers_.size(); ++i) {
      const auto& matcher = matchers_.at(i);
      const FieldMatchResult maybe_match = matcher.first->match(matching_data);

      // One of the matchers don't have enough information, bail on evaluating the match.
      if (maybe_match.match_state_ == MatchState::UnableToMatch) {
        return {MatchState::UnableToMatch, {}, nullptr};
      }
      // Check for a match.
      if (maybe_match.result()) {
        // Provide a reentrant ListMatcher to continue traversal from the next index.
        return {MatchState::MatchComplete, matcher.second,
                std::make_unique<ListMatcherReentrant<DataType>>(*this, i + 1)};
      }
    }
    // No matches found.
    return {MatchState::MatchComplete, on_no_match_, nullptr};
  }

  // MatchTree interface match logic implementation.
  typename MatchTree<DataType>::MatchResult match(const DataType& matching_data) override {
    return matchImpl(matching_data);
  }

private:
  absl::optional<OnMatch<DataType>> on_no_match_;
  std::vector<std::pair<FieldMatcherPtr<DataType>, OnMatch<DataType>>> matchers_;
};

// Referential class to allow for re-entry into a ListMatcher after an initial match.
template <class DataType> class ListMatcherReentrant : public MatchTree<DataType> {
public:
  explicit ListMatcherReentrant(ListMatcher<DataType>& parent_matcher, int starting_index)
      : parent_matcher_(parent_matcher), starting_index_(starting_index) {}

  typename MatchTree<DataType>::MatchResult match(const DataType& matching_data) override {
    return parent_matcher_.matchImpl(matching_data, starting_index_);
  }

private:
  ListMatcher<DataType>& parent_matcher_;
  int starting_index_;
};

} // namespace Matcher
} // namespace Envoy
