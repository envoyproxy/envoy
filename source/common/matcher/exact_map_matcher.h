#pragma once

#include "envoy/matcher/matcher.h"

namespace Envoy {
namespace Matcher {

/**
 * Implementation of a `sublinear` match tree that provides O(1) lookup of exact values,
 * with one OnMatch per result.
 */
template <class DataType>
class ExactMapMatcher : public MatchTree<DataType>, Logger::Loggable<Logger::Id::matcher> {
public:
  ExactMapMatcher(DataInputPtr<DataType>&& data_input,
                  absl::optional<OnMatch<DataType>> on_no_match)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)) {}

  typename MatchTree<DataType>::MatchResult match(const DataType& data) override {
    const auto input = data_input_->get(data);
    ENVOY_LOG(debug, "Attempting to match {}", input);
    if (input.data_availability_ == DataInputGetResult::DataAvailability::NotAvailable) {
      return {MatchState::UnableToMatch, absl::nullopt};
    }

    if (!input.data_) {
      return {MatchState::MatchComplete, on_no_match_};
    }

    const auto itr = children_.find(*input.data_);
    if (itr != children_.end()) {
      const auto result = itr->second;

      if (result.matcher_) {
        return result.matcher_->match(data);
      } else {
        return {MatchState::MatchComplete, OnMatch<DataType>{result.action_cb_, nullptr}};
      }
    } else if (input.data_availability_ ==
               DataInputGetResult::DataAvailability::MoreDataMightBeAvailable) {
      // It's possible that we were attempting a lookup with a partial value, so delay matching
      // until we know that we actually failed.
      return {MatchState::UnableToMatch, absl::nullopt};
    }

    return {MatchState::MatchComplete, on_no_match_};
  }

  void addChild(std::string value, OnMatch<DataType>&& on_match) {
    const auto itr_and_exists = children_.emplace(value, std::move(on_match));
    ASSERT(itr_and_exists.second);
  }

private:
  absl::flat_hash_map<std::string, OnMatch<DataType>> children_;
  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;
};
} // namespace Matcher
} // namespace Envoy