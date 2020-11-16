
#pragma once

#include "envoy/matcher/matcher.h"

namespace Envoy {
namespace Matcher {

template <class DataType> class FieldMatcher {
public:
  virtual ~FieldMatcher() = default;

  virtual absl::optional<bool> match(const DataType& data) PURE;
};
template <class DataType> using FieldMatcherPtr = std::unique_ptr<FieldMatcher<DataType>>;

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

template <class DataType> class SingleFieldMatcher : public FieldMatcher<DataType> {
public:
  SingleFieldMatcher(DataInputPtr<DataType>&& data_input, InputMatcherPtr&& input_matcher)
      : data_input_(std::move(data_input)), input_matcher_(std::move(input_matcher)) {}

  absl::optional<bool> match(const DataType& data) override {
    const auto input = data_input_->get(data);

    ENVOY_LOG_MISC(debug, "Attempting to match {}", input);
    if (input.not_available_yet) {
      return absl::nullopt;
    }

    const auto current_match = input_matcher_->match(input.data_);
    ENVOY_LOG_MISC(debug, "Match result: {}", current_match);
    if (!current_match && input.more_data_available) {
      return absl::nullopt;
    }

    // TODO(snowp): Expose the optional so that we can match on not present.
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