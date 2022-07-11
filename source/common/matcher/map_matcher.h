#pragma once

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
  MapMatcher(DataInputPtr<DataType>&& data_input, absl::optional<OnMatch<DataType>> on_no_match)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)) {}

  // Adds a child to the map.
  virtual void addChild(std::string value, OnMatch<DataType>&& on_match) PURE;

  typename MatchTree<DataType>::MatchResult match(const DataType& data) override {
    const auto input = data_input_->get(data);
    ENVOY_LOG(trace, "Attempting to match {}", input);
    if (input.data_availability_ == DataInputGetResult::DataAvailability::NotAvailable) {
      return {MatchState::UnableToMatch, absl::nullopt};
    }

    if (input.data_.isNull()) {
      return {MatchState::MatchComplete, on_no_match_};
    }

    const auto result = doMatch(input.data_);

    if (result) {
      if (result->matcher_) {
        return result->matcher_->match(data);
      } else {
        return {MatchState::MatchComplete, OnMatch<DataType>{result->action_cb_, nullptr}};
      }
    } else if (input.data_availability_ ==
               DataInputGetResult::DataAvailability::MoreDataMightBeAvailable) {
      // It's possible that we were attempting a lookup with a partial value, so delay matching
      // until we know that we actually failed.
      return {MatchState::UnableToMatch, absl::nullopt};
    }

    return {MatchState::MatchComplete, on_no_match_};
  }

protected:
  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;

  // The inner match method. Attempts to match against the resulting data string. If the match
  // result was determined, the OnMatch will be returned. If a match result was determined to be no
  // match, {} will be returned.
  virtual absl::optional<OnMatch<DataType>> doMatch(absl::string_view data) PURE;

private:
  absl::optional<OnMatch<DataType>> doMatch(const InputValue& data) {
    switch (data.kind()) {
    case InputValue::Kind::Null:
      return absl::nullopt;
    case InputValue::Kind::String:
      return doMatch(data.asString());
    case InputValue::Kind::Int:
      return doMatch(absl::StrCat(data.asInt()));
    case InputValue::Kind::List: {
      for (const auto& elt : data.asList()) {
        const auto result = doMatch(elt);
        if (result) {
          return result;
        }
      }
      return absl::nullopt;
    }
    }
  }
};

} // namespace Matcher
} // namespace Envoy
