#pragma once

#include "envoy/matcher/matcher.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Matcher {

/**
 * The result of a field match.
 */
struct FieldMatchResult {
  // Encodes whether we were able to perform the match.
  MatchState match_state_;

  // The result, if matching was completed.
  absl::optional<bool> result_;

  // The unwrapped result. Should only be called if match_state_ == MatchComplete.
  bool result() const {
    ASSERT(match_state_ == MatchState::MatchComplete);
    ASSERT(result_.has_value());
    return *result_;
  }
};

/**
 * Base class for matching against a single input.
 */
template <class DataType> class FieldMatcher {
public:
  virtual ~FieldMatcher() = default;

  /**
   * Attempts to match against the provided data.
   * @returns absl::optional<bool> if matching was possible, the result of the match. Otherwise
   * absl::nullopt if the data is not available.
   */
  virtual FieldMatchResult match(const DataType& data) PURE;
};
template <class DataType> using FieldMatcherPtr = std::unique_ptr<FieldMatcher<DataType>>;

/**
 * A FieldMatcher that attempts to match multiple FieldMatchers, evaluating to true iff all the
 * FieldMatchers evaluate to true.
 *
 * If any of the underlying FieldMatchers are unable to produce a result, absl::nullopt is returned.
 */
template <class DataType> class AllFieldMatcher : public FieldMatcher<DataType> {
public:
  explicit AllFieldMatcher(std::vector<FieldMatcherPtr<DataType>>&& matchers)
      : matchers_(std::move(matchers)) {}

  FieldMatchResult match(const DataType& data) override {
    for (const auto& matcher : matchers_) {
      const auto result = matcher->match(data);

      // If we are unable to decide on a match at this point, propagate this up to defer
      // the match result until we have the requisite data.
      if (result.match_state_ == MatchState::UnableToMatch) {
        return result;
      }

      if (!result.result()) {
        return result;
      }
    }

    return {MatchState::MatchComplete, true};
  }

private:
  const std::vector<FieldMatcherPtr<DataType>> matchers_;
};

/**
 * A FieldMatcher that attempts to match multiple FieldMatchers, evaluating to true iff any of the
 * FieldMatchers evaluate to true.
 *
 * If any of the underlying FieldMatchers are unable to produce a result before we see a successful
 * match, absl::nullopt is returned.
 */
template <class DataType> class AnyFieldMatcher : public FieldMatcher<DataType> {
public:
  explicit AnyFieldMatcher(std::vector<FieldMatcherPtr<DataType>>&& matchers)
      : matchers_(std::move(matchers)) {}

  FieldMatchResult match(const DataType& data) override {
    bool unable_to_match_some_matchers = false;
    for (const auto& matcher : matchers_) {
      const auto result = matcher->match(data);

      if (result.match_state_ == MatchState::UnableToMatch) {
        unable_to_match_some_matchers = true;
        continue;
      }

      if (result.result()) {
        return {MatchState::MatchComplete, true};
      }
    }

    // If we didn't find a successful match but not all matchers could be evaluated,
    // return UnableToMatch to defer the match result.
    if (unable_to_match_some_matchers) {
      return {MatchState::UnableToMatch, absl::nullopt};
    }

    return {MatchState::MatchComplete, false};
  }

private:
  const std::vector<FieldMatcherPtr<DataType>> matchers_;
};

/**
 * A FieldMatcher that returns the invert of a FieldMatcher.
 */
template <class DataType> class NotFieldMatcher : public FieldMatcher<DataType> {
public:
  explicit NotFieldMatcher(FieldMatcherPtr<DataType> matcher) : matcher_(std::move(matcher)) {}

  FieldMatchResult match(const DataType& data) override {
    const auto result = matcher_->match(data);
    if (result.match_state_ == MatchState::UnableToMatch) {
      return result;
    }

    return {MatchState::MatchComplete, !result.result()};
  }

private:
  const FieldMatcherPtr<DataType> matcher_;
};

/**
 * Implementation of a FieldMatcher that extracts an input value from the provided data and attempts
 * to match using an InputMatcher. absl::nullopt is returned whenever the data is not available or
 * if we failed to match and there is more data available.
 * A consequence of this is that if a match result is desired, care should be taken so that matching
 * is done with all the data available at some point.
 */
template <class DataType>
class SingleFieldMatcher : public FieldMatcher<DataType>, Logger::Loggable<Logger::Id::matcher> {
public:
  static absl::StatusOr<std::unique_ptr<SingleFieldMatcher<DataType>>>
  create(DataInputPtr<DataType>&& data_input, InputMatcherPtr&& input_matcher) {
    auto supported_input_types = input_matcher->supportedDataInputTypes();
    if (supported_input_types.find(data_input->dataInputType()) == supported_input_types.end()) {
      std::string supported_types =
          absl::StrJoin(supported_input_types.begin(), supported_input_types.end(), ", ");
      return absl::InvalidArgumentError(
          absl::StrCat("Unsupported data input type: ", data_input->dataInputType(),
                       ". The matcher supports input type: ", supported_types));
    }

    return std::unique_ptr<SingleFieldMatcher<DataType>>{
        new SingleFieldMatcher<DataType>(std::move(data_input), std::move(input_matcher))};
  }

  FieldMatchResult match(const DataType& data) override {
    const auto input = data_input_->get(data);

    ENVOY_LOG(trace, "Attempting to match {}", input);
    if (input.data_availability_ == DataInputGetResult::DataAvailability::NotAvailable) {
      return {MatchState::UnableToMatch, absl::nullopt};
    }

    bool current_match = input_matcher_->match(input.data_);
    if (!current_match && input.data_availability_ ==
                              DataInputGetResult::DataAvailability::MoreDataMightBeAvailable) {
      ENVOY_LOG(trace, "No match yet; delaying result as more data might be available.");
      return {MatchState::UnableToMatch, absl::nullopt};
    }

    ENVOY_LOG(trace, "Match result: {}", current_match);

    return {MatchState::MatchComplete, current_match};
  }

private:
  SingleFieldMatcher(DataInputPtr<DataType>&& data_input, InputMatcherPtr&& input_matcher)
      : data_input_(std::move(data_input)), input_matcher_(std::move(input_matcher)) {}

  const DataInputPtr<DataType> data_input_;
  const InputMatcherPtr input_matcher_;
};

template <class DataType>
using SingleFieldMatcherPtr = std::unique_ptr<SingleFieldMatcher<DataType>>;
} // namespace Matcher
} // namespace Envoy
