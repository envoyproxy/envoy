#pragma once

#include <string>

#include "envoy/matcher/matcher.h"

namespace Envoy {
namespace Matcher {

/**
 * Implementation of a map matcher which performs matches against the data provided by DataType.
 * If the match could not be completed, {MatchState::UnableToMatch, {}} will be returned. If the
 * match result was determined, {MatchState::MatchComplete, OnMatch} will be returned. If a match
 * result was determined to be no match, {MatchState::MatchComplete, {}} will be returned.
 */
template <class DataType>
class MapMatcher : public MatchTree<DataType>, Logger::Loggable<Logger::Id::matcher> {
public:
  // Adds a child to the map.
  virtual void addChild(std::string value, OnMatch<DataType>&& on_match) PURE;

  typename MatchTree<DataType>::MatchResult
  match(const DataType& data, SkippedMatchCb<DataType> skipped_match_cb = nullptr) override {
    const auto input = data_input_->get(data);
    ENVOY_LOG(trace, "Attempting to match {}", input);
    if (input.data_availability_ == DataInputGetResult::DataAvailability::NotAvailable) {
      return {MatchState::UnableToMatch, absl::nullopt};
    }

    // Returns `on_no_match` when input data is empty. (i.e., is absl::monostate).
    if (absl::holds_alternative<absl::monostate>(input.data_)) {
      return MatchTree<DataType>::handleRecursionAndSkips(on_no_match_, data, skipped_match_cb);
    }

    const typename MatchTree<DataType>::MatchResult result =
        doMatch(data, absl::get<std::string>(input.data_), skipped_match_cb);
    if (result.match_state_ != MatchState::MatchComplete) {
      return result;
    }
    // No match.
    if (!result.on_match_.has_value()) {
      // Match failed.
      if (input.data_availability_ ==
          DataInputGetResult::DataAvailability::MoreDataMightBeAvailable) {
        return {MatchState::UnableToMatch, absl::nullopt};
      }
      // No-match, return on_no_match after keep_matching and/or recursion handling.
      return MatchTree<DataType>::handleRecursionAndSkips(on_no_match_, data, skipped_match_cb);
    }

    // Handle recursion and keep_matching.
    auto processed_result =
        MatchTree<DataType>::handleRecursionAndSkips(result.on_match_, data, skipped_match_cb);
    // Matched or failed nested matching.
    if (processed_result.match_state_ != MatchState::MatchComplete ||
        processed_result.on_match_.has_value()) {
      return processed_result;
    }
    // No-match, return on_no_match after keep_matching and/or recursion handling.
    return MatchTree<DataType>::handleRecursionAndSkips(on_no_match_, data, skipped_match_cb);
  }

  template <class DataType2, class ActionFactoryContext> friend class MatchTreeFactory;
  MapMatcher(DataInputPtr<DataType>&& data_input, absl::optional<OnMatch<DataType>> on_no_match,
             absl::Status& creation_status)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)) {
    auto input_type = data_input_->dataInputType();
    if (input_type != DefaultMatchingDataType) {
      creation_status = absl::InvalidArgumentError(
          absl::StrCat("Unsupported data input type: ", input_type,
                       ", currently only string type is supported in map matcher"));
    }
  }

  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;

  // The inner match method. Attempts to match against the resulting data string. If the match
  // result was determined, the OnMatch will be returned. If a match result was determined to be no
  // match, {} will be returned.
  virtual typename MatchTree<DataType>::MatchResult
  doMatch(const DataType& data, absl::string_view key,
          SkippedMatchCb<DataType> skipped_match_cb) PURE;
};

} // namespace Matcher
} // namespace Envoy
