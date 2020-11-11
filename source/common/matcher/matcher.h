#pragma once

#include <memory>
#include <variant>

#include "envoy/config/common/matcher/v3/matcher.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/matcher/matcher.h"

#include "common/common/assert.h"

#include "extensions/common/matcher/matcher.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {

// TODO(snowp): Make this a class that tracks the progress to speed up subsequent traversals.
static inline TypedExtensionConfigOpt evaluateMatch(MatchTree& match_tree,
                                                    const MatchingData& data) {
  const auto result = match_tree.match(data);
  if (!result.first) {
    return absl::nullopt;
  }

  if (result.second->second) {
    return evaluateMatch(*result.second->second, data);
  }

  return *result.second->first;
}

class DataInput {
public:
  virtual ~DataInput() = default;

  virtual absl::string_view get(const MatchingData& data) PURE;
};

using DataInputPtr = std::unique_ptr<DataInput>;

class MultimapMatcher : public MatchTree {
public:
  MultimapMatcher(DataInputPtr&& data_input, absl::optional<OnMatch> on_no_match)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)) {}

  MatchResult match(const MatchingData& data) override {
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

  void addChild(std::string value, OnMatch&& on_match) { children_[value] = std::move(on_match); }

private:
  absl::flat_hash_map<std::string, OnMatch> children_;
  const DataInputPtr data_input_;
  const absl::optional<OnMatch> on_no_match_;
};

class FieldMatcher {
public:
  virtual ~FieldMatcher() = default;

  virtual absl::optional<bool> match(const MatchingData& data) PURE;
};

using FieldMatcherPtr = std::unique_ptr<FieldMatcher>;

class ListMatcher : public MatchTree {
public:
  explicit ListMatcher(absl::optional<OnMatch> on_no_match) : on_no_match_(on_no_match) {}

  MatchResult match(const MatchingData& matching_data) override {
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

  void addMatcher(FieldMatcherPtr&& matcher, OnMatch action) {
    matchers_.push_back({std::move(matcher), std::move(action)});
  }

private:
  absl::optional<OnMatch> on_no_match_;
  std::vector<std::pair<FieldMatcherPtr, OnMatch>> matchers_;
};

class DataInputFactory : public Config::TypedFactory {
public:
  virtual ~DataInputFactory() = default;

  virtual DataInputPtr create() PURE;
};

/**
 * Recursively constructs a MatchTree from a protobuf configuration.
 */
class MatchTreeFactory {
public:
  MatchTreeSharedPtr create(const envoy::config::common::matcher::v3::Matcher& config);

private:
  MatchTreeSharedPtr createListMatcher(const envoy::config::common::matcher::v3::Matcher& config);

  MatchTreeSharedPtr createTreeMatcher(const envoy::config::common::matcher::v3::Matcher& matcher);

  OnMatch createOnMatch(const envoy::config::common::matcher::v3::Matcher::OnMatch& on_match);

  DataInputPtr createDataInput(const envoy::config::core::v3::TypedExtensionConfig& config);
};
} // namespace Envoy
