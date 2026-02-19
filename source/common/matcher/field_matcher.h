#pragma once

#include "envoy/matcher/matcher.h"

#include "absl/strings/str_join.h"

namespace Envoy {
namespace Matcher {

/**
 * Base class for matching against a single input.
 */
template <class DataType> class FieldMatcher {
public:
  virtual ~FieldMatcher() = default;

  /**
   * Attempts to match against the provided data.
   */
  virtual MatchResult match(const DataType& data) PURE;
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

  MatchResult match(const DataType& data) override {
    for (const auto& matcher : matchers_) {
      const MatchResult result = matcher->match(data);

      // If we are unable to decide on a match at this point, propagate this up to defer
      // the match result until we have the requisite data.
      if (result == MatchResult::InsufficientData) {
        return result;
      }

      if (result == MatchResult::NoMatch) {
        return result;
      }
    }

    return MatchResult::Matched;
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

  MatchResult match(const DataType& data) override {
    bool unable_to_match_some_matchers = false;
    for (const auto& matcher : matchers_) {
      const MatchResult result = matcher->match(data);

      if (result == MatchResult::InsufficientData) {
        unable_to_match_some_matchers = true;
        continue;
      }

      if (result == MatchResult::Matched) {
        return result;
      }
    }

    // If we didn't find a successful match but not all matchers could be evaluated,
    // return InsufficientData to defer the match result.
    if (unable_to_match_some_matchers) {
      return MatchResult::InsufficientData;
    }

    return MatchResult::NoMatch;
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

  MatchResult match(const DataType& data) override {
    const MatchResult result = matcher_->match(data);
    if (result == MatchResult::InsufficientData) {
      return result;
    }
    return (result == MatchResult::Matched) ? MatchResult::NoMatch : MatchResult::Matched;
  }

private:
  const FieldMatcherPtr<DataType> matcher_;
};

/**
 * Implementation of a FieldMatcher that extracts an input value from the provided data and attempts
 * to match using an InputMatcher. InsufficientData is returned whenever the data is not available
 * or if we failed to match and there may be more data available later. A consequence of this is
 * that if a match result is desired, care should be taken so that matching is done with all the
 * data available at some point.
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

  MatchResult match(const DataType& data) override {
    const auto input = data_input_->get(data);

    if (input.data_availability_ == DataInputGetResult::DataAvailability::NotAvailable) {
      return MatchResult::InsufficientData;
    }

    MatchResult current_match = input_matcher_->match(input.data_);
    if (current_match != MatchResult::Matched &&
        input.data_availability_ ==
            DataInputGetResult::DataAvailability::MoreDataMightBeAvailable) {
      return MatchResult::InsufficientData;
    }

    return current_match;
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
