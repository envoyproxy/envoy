#pragma once

#include <memory>
#include <variant>

#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/matcher/matcher.h"

#include "common/common/assert.h"
#include "common/config/utility.h"

#include "extensions/common/matcher/matcher.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {

// TODO(snowp): Make this a class that tracks the progress to speed up subsequent traversals.
template<class DataType>
static inline TypedExtensionConfigOpt evaluateMatch(MatchTree<DataType>& match_tree,
                                                    const DataType& data) {
  const auto result = match_tree.match(data);
  if (!result.first) {
    return absl::nullopt;
  }

  if (result.second->second) {
    return evaluateMatch(*result.second->second, data);
  }

  return *result.second->first;
}

template<class DataType>
class DataInput {
public:
  virtual ~DataInput() = default;

  virtual absl::string_view get(const DataType& data) PURE;
};

template<class DataType>
using DataInputPtr = std::unique_ptr<DataInput<DataType>>;

template<class DataType>
class MultimapMatcher : public MatchTree<DataType> {
public:
  MultimapMatcher(DataInputPtr<DataType>&& data_input, absl::optional<OnMatch<DataType>> on_no_match)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)) {}

  typename MatchTree<DataType>::MatchResult match(const DataType& data) override {
    const auto itr = children_.find(data_input_->get(data));
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

  void addChild(std::string value, OnMatch<DataType>&& on_match) { children_[value] = std::move(on_match); }

private:
  absl::flat_hash_map<std::string, OnMatch<DataType>> children_;
  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;
};

template<class DataType>
class FieldMatcher {
public:
  virtual ~FieldMatcher() = default;

  virtual absl::optional<bool> match(const DataType& data) PURE;
};

template<class DataType>
using FieldMatcherPtr = std::unique_ptr<FieldMatcher<DataType>>;

template<class DataType>
class ListMatcher : public MatchTree<DataType> {
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

template<class DataType>
class DataInputFactory : public Config::TypedFactory {
public:
  virtual ~DataInputFactory() = default;

  virtual DataInputPtr<DataType> create() PURE;
};

/**
 * Recursively constructs a MatchTree from a protobuf configuration.
 */
template<class DataType>
class MatchTreeFactory {
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
    auto leaf = std::make_shared<ListMatcher<DataType>>(createOnMatch(config.on_no_match()));

    // for (const auto& _ : config.matcher_list().matchers()) {
    //   // TODO
    // }

    return leaf;
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
    return factory.create();
  }
};
} // namespace Envoy
