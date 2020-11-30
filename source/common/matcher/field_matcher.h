
#pragma once

#include "envoy/matcher/matcher.h"

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
   * @returns absl::optional<bool> if matching was possible, the result of the match. Otherwise
   * absl::nullopt if the data is not available.
   */
  virtual absl::optional<bool> match(const DataType& data) PURE;
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

  absl::optional<bool> match(const DataType& data) override {
    for (const auto& matcher : matchers_) {
      const auto result = matcher->match(data);

      if (!result) {
        return result;
      }

      if (!*result) {
        return false;
      }
    }

    return true;
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

  absl::optional<bool> match(const DataType& data) override {
    for (const auto& matcher : matchers_) {
      const auto result = matcher->match(data);

      if (!result) {
        return result;
      }

      if (*result) {
        return true;
      }
    }

    return false;
  }

private:
  const std::vector<FieldMatcherPtr<DataType>> matchers_;
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
  SingleFieldMatcher(DataInputPtr<DataType>&& data_input, InputMatcherPtr&& input_matcher)
      : data_input_(std::move(data_input)), input_matcher_(std::move(input_matcher)) {}

  absl::optional<bool> match(const DataType& data) override {
    const auto input = data_input_->get(data);

    ENVOY_LOG(debug, "Attempting to match {}", input);
    if (input.data_availability_ == DataInputGetResult::DataAvailability::NotAvailable) {
      return absl::nullopt;
    }

    const auto current_match = input_matcher_->match(input.data_);
    if (!current_match && input.data_availability_ ==
                              DataInputGetResult::DataAvailability::MoreDataMightBeAvailable) {
      ENVOY_LOG(debug, "No match yet; delaying result as more data might be available.");
      return absl::nullopt;
    }

    ENVOY_LOG(debug, "Match result: {}", current_match);

    return current_match;
  }

private:
  const DataInputPtr<DataType> data_input_;
  const InputMatcherPtr input_matcher_;
};

template <class DataType>
using SingleFieldMatcherPtr = std::unique_ptr<SingleFieldMatcher<DataType>>;
} // namespace Matcher
} // namespace Envoy