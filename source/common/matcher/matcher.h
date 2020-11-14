#pragma once

#include <memory>
#include <variant>

#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/matcher/matcher.h"

#include "common/common/assert.h"
#include "common/common/matchers.h"
#include "common/config/utility.h"

#include "extensions/common/matcher/matcher.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "external/envoy_api/envoy/config/core/v3/extension.pb.h"

namespace Envoy {

struct MaybeMatchResult {
  const TypedExtensionConfigOpt result_;
  const bool final_;
};

// TODO(snowp): Make this a class that tracks the progress to speed up subsequent traversals.
template <class DataType>
static inline MaybeMatchResult evaluateMatch(MatchTree<DataType>& match_tree,
                                             const DataType& data) {
  const auto result = match_tree.match(data);
  if (!result.first) {
    return MaybeMatchResult{absl::nullopt, false};
  }

  if (result.second->second) {
    return evaluateMatch(*result.second->second, data);
  }

  return MaybeMatchResult{*result.second->first, true};
}

struct DataInputGetResult {
  // The data has not arrived yet, nothing to match against.
  bool not_available_yet;
  // Some data is available, so matching can be attempted. If it fails, more data might arrive which
  // could satisfy the match.
  bool more_data_available;
  // The data is available: result of looking up the data. If the lookup failed against partial or
  // complete data this will remain absl::nullopt.
  absl::optional<absl::string_view> data_;
};

template <class DataType> class DataInput {
public:
  virtual ~DataInput() = default;

  virtual DataInputGetResult get(const DataType& data) PURE;
};

template <class DataType> using DataInputPtr = std::unique_ptr<DataInput<DataType>>;

template <class DataType> class MultimapMatcher : public MatchTree<DataType> {
public:
  MultimapMatcher(DataInputPtr<DataType>&& data_input,
                  absl::optional<OnMatch<DataType>> on_no_match)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)) {}

  typename MatchTree<DataType>::MatchResult match(const DataType& data) override {
    const auto input = data_input_->get(data);
    if (input.not_available_yet) {
      return {false, absl::nullopt};
    }

    if (!input.data_) {
      return {true, on_no_match_};
    }

    const auto itr = children_.find(*input.data_);
    if (itr != children_.end()) {
      const auto result = itr->second;

      if (!result.first) {
        return result.second->match(data);
      } else {
        return {true, std::make_pair(result.first, nullptr)};
      }
    }

    return {true, on_no_match_};
  }

  void addChild(std::string value, OnMatch<DataType>&& on_match) {
    children_[value] = std::move(on_match);
  }

private:
  absl::flat_hash_map<std::string, OnMatch<DataType>> children_;
  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;
};

class InputMatcher {
public:
  virtual ~InputMatcher() = default;

  virtual absl::optional<bool> match(absl::string_view input) PURE;
};

class ValueInputMatcher : public InputMatcher {
public:
  explicit ValueInputMatcher(const envoy::type::matcher::v3::ValueMatcher& matcher)
      : matcher_(Matchers::ValueMatcher::create(matcher)) {}

  absl::optional<bool> match(absl::string_view input) override {
    ProtobufWkt::Value value;
    value.set_string_value(std::string(input));
    const auto r = matcher_->match(value);

    std::cout << "input " << input << " " << r << std::endl;
    return r;
  }

private:
  const Matchers::ValueMatcherConstSharedPtr matcher_;
};

using InputMatcherPtr = std::unique_ptr<InputMatcher>;

class InputMatcherFactory : public Config::TypedFactory {
public:
  virtual ~InputMatcherFactory() = default;

  virtual InputMatcherPtr create(const envoy::config::core::v3::TypedExtensionConfig& config) PURE;
};

template <class DataType> class FieldPredicate {};

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

      // Not ready yet.
      if (!result) {
        return result;
      }

      // Field did not match.
      if (!*result) {
        std::cout << "one of the fields did not match" << std::endl;
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

      // Not ready yet.
      if (!result) {
        return result;
      }

      // One of the values matched.
      if (result && *result) {
        return true;
      }
    }

    std::cout << "no field matched" << std::endl;

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
    if (input.not_available_yet) {
      return absl::nullopt;
    }

    if (!input.data_) {
      return false;
    }

    const auto current_match = input_matcher_->match(*input.data_);
    if (input.data_) {
      std::cout << "seeing " << *input.data_ << std::endl;
    }
    if ((!current_match || (current_match && !*current_match)) && input.more_data_available) {
      std::cout << "deferring" << *input.data_ << std::endl;
      return absl::nullopt;
    }
    // std::cout << "m: " << current_match << std::endl;
    std::cout << "more: " << input.more_data_available << std::endl;

    // TODO(snowp): Expose the optional so that we can match on not present.
    return current_match;
  }

private:
  const DataInputPtr<DataType> data_input_;
  const InputMatcherPtr input_matcher_;
};

template <class DataType> class ListMatcher : public MatchTree<DataType> {
public:
  explicit ListMatcher(absl::optional<OnMatch<DataType>> on_no_match) : on_no_match_(on_no_match) {}

  typename MatchTree<DataType>::MatchResult match(const DataType& matching_data) override {
    for (const auto& matcher : matchers_) {
      const auto maybe_match = matcher.first->match(matching_data);
      // One of the matchers don't have enough information, delay.
      if (!maybe_match) {
        return {false, {}};
      }

      if (*maybe_match) {
        return {true, matcher.second};
      }
    }

    return {true, on_no_match_};
  }

  void addMatcher(FieldMatcherPtr<DataType>&& matcher, OnMatch<DataType> action) {
    matchers_.push_back({std::move(matcher), std::move(action)});
  }

private:
  absl::optional<OnMatch<DataType>> on_no_match_;
  std::vector<std::pair<FieldMatcherPtr<DataType>, OnMatch<DataType>>> matchers_;
};

template <class DataType> class DataInputFactory : public Config::TypedFactory {
public:
  virtual ~DataInputFactory() = default;

  virtual DataInputPtr<DataType>
  create(const envoy::config::core::v3::TypedExtensionConfig& config) PURE;
};

/**
 * Recursively constructs a MatchTree from a protobuf configuration.
 */
template <class DataType> class MatchTreeFactory {
public:
  MatchTreeSharedPtr<DataType> create(const envoy::config::common::matcher::v3::Matcher& config) {
    if (config.has_matcher_tree()) {
      return createTreeMatcher(config);
    } else if (config.has_matcher_list()) {
      return createListMatcher(config);
    } else {
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

private:
  MatchTreeSharedPtr<DataType>
  createListMatcher(const envoy::config::common::matcher::v3::Matcher& config) {
    auto list_matcher =
        std::make_shared<ListMatcher<DataType>>(createOnMatch(config.on_no_match()));

    for (const auto& matcher : config.matcher_list().matchers()) {
      list_matcher->addMatcher(createFieldMatcher(matcher.predicate()),
                               createOnMatch(matcher.on_match()));
    }

    return list_matcher;
  }

  FieldMatcherPtr<DataType> createFieldMatcher(
      const envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate& field_predicate) {
    switch (field_predicate.match_type_case()) {
    case (envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::kSinglePredicate):

      return std::make_unique<SingleFieldMatcher<DataType>>(
          createDataInput(field_predicate.single_predicate().input()),
          createInputMatcher(field_predicate.single_predicate()));
    case (envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::kOrMatcher): {
      std::vector<FieldMatcherPtr<DataType>> sub_matchers;
      for (const auto& predicate : field_predicate.or_matcher().predicate()) {
        sub_matchers.emplace_back(createFieldMatcher(predicate));
      }

      return std::make_unique<AnyFieldMatcher<DataType>>(std::move(sub_matchers));
    }
    case (envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::kAndMatcher): {
      std::vector<FieldMatcherPtr<DataType>> sub_matchers;
      for (const auto& predicate : field_predicate.and_matcher().predicate()) {
        sub_matchers.emplace_back(createFieldMatcher(predicate));
      }

      return std::make_unique<AllFieldMatcher<DataType>>(std::move(sub_matchers));
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  MatchTreeSharedPtr<DataType>
  createTreeMatcher(const envoy::config::common::matcher::v3::Matcher& matcher) {
    auto multimap_matcher = std::make_shared<MultimapMatcher<DataType>>(
        createDataInput(matcher.matcher_tree().input()), createOnMatch(matcher.on_no_match()));

    for (const auto& children : matcher.matcher_tree().exact_match_map().map()) {
      multimap_matcher->addChild(children.first, MatchTreeFactory::createOnMatch(children.second));
    }

    return multimap_matcher;
  }
  OnMatch<DataType>
  createOnMatch(const envoy::config::common::matcher::v3::Matcher::OnMatch& on_match) {
    if (on_match.has_matcher()) {
      return {{}, create(on_match.matcher())};
    } else if (on_match.has_action()) {
      return {on_match.action(), {}};
    } else {
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  DataInputPtr<DataType>
  createDataInput(const envoy::config::core::v3::TypedExtensionConfig& config) {
    auto& factory = Config::Utility::getAndCheckFactory<DataInputFactory<DataType>>(config);
    return factory.create(config);
  }

  InputMatcherPtr createInputMatcher(
      const envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::SinglePredicate&
          predicate) {
    switch (predicate.matcher_case()) {
    case envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::SinglePredicate::
        kValueMatch:
      return std::make_unique<ValueInputMatcher>(predicate.value_match());
    case envoy::config::common::matcher::v3::Matcher::MatcherList::Predicate::SinglePredicate::
        kCustomMatch: {
      auto& factory =
          Config::Utility::getAndCheckFactory<InputMatcherFactory>(predicate.custom_match());
      return factory.create(predicate.custom_match());
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
};
} // namespace Envoy
