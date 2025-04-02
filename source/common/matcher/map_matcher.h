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

  typename MatchTree<DataType>::MatchResult match(const DataType& data) override {
    const auto input = data_input_->get(data);
    ENVOY_LOG(trace, "Attempting to match {}", input);
    if (input.data_availability_ == DataInputGetResult::DataAvailability::NotAvailable) {
      return {MatchState::UnableToMatch, absl::nullopt};
    }

    // Returns `on_no_match` when input data is empty. (i.e., is absl::monostate).
    if (absl::holds_alternative<absl::monostate>(input.data_)) {
      return {MatchState::MatchComplete, on_no_match_};
    }

    absl::string_view target = absl::get<std::string>(input.data_);
    while (true) {
      absl::optional<OnMatch<DataType>> result = doMatch(target);
      if (result == absl::nullopt) {
        break;
      }
      if (result->matcher_) {
        if (!retry_shorter_) {
          return result->matcher_->match(data);
        }
        typename MatchTree<DataType>::MatchResult match = result->matcher_->match(data);
        if (match.match_state_ != MatchState::MatchComplete || match.on_match_.has_value()) {
          return match;
        }
        // If a subtree lookup found neither match nor on_no_match, and retry_shorter_ is set,
        // we want to try again with a shorter matching prefix if one can be found.
        size_t prev_length = matchedPrefixLength(target);
        if (prev_length == 0) {
          break;
        }
        target = target.substr(0, prev_length - 1);
      } else {
        return {MatchState::MatchComplete, OnMatch<DataType>{result->action_cb_, nullptr}};
      }
    }
    if (input.data_availability_ ==
        DataInputGetResult::DataAvailability::MoreDataMightBeAvailable) {
      // It's possible that we were attempting a lookup with a partial value, so delay matching
      // until we know that we actually failed.
      return {MatchState::UnableToMatch, absl::nullopt};
    }

    return {MatchState::MatchComplete, on_no_match_};
  }

protected:
  template <class DataType2, class ActionFactoryContext> friend class MatchTreeFactory;
  MapMatcher(DataInputPtr<DataType>&& data_input, absl::optional<OnMatch<DataType>> on_no_match,
             absl::Status& creation_status, bool retry_shorter)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)),
        retry_shorter_(retry_shorter) {
    auto input_type = data_input_->dataInputType();
    if (input_type != DefaultMatchingDataType) {
      creation_status = absl::InvalidArgumentError(
          absl::StrCat("Unsupported data input type: ", input_type,
                       ", currently only string type is supported in map matcher"));
    }
  }

  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;
  const bool retry_shorter_;

  // The inner match method. Attempts to match against the resulting data string. If the match
  // result was determined, the OnMatch will be returned. If a match result was determined to be no
  // match, {} will be returned.
  virtual absl::optional<OnMatch<DataType>> doMatch(absl::string_view data) PURE;
  virtual size_t matchedPrefixLength(absl::string_view) { return 0; }
};

} // namespace Matcher
} // namespace Envoy
