#pragma once

#include "source/common/matcher/map_matcher.h"

namespace Envoy {
namespace Matcher {

/**
 * Implementation of a `sublinear` match tree that provides O(1) lookup of exact values,
 * with one OnMatch per result.
 */
template <class DataType> class ExactMapMatcher : public MapMatcher<DataType> {
public:
  ExactMapMatcher(DataInputPtr<DataType>&& data_input,
                  absl::optional<OnMatch<DataType>> on_no_match)
      : MapMatcher<DataType>(std::move(data_input), std::move(on_no_match)) {}

  void addChild(std::string value, OnMatch<DataType>&& on_match) override {
    const auto itr_and_exists = children_.emplace(value, std::move(on_match));
    ASSERT(itr_and_exists.second);
  }

protected:
  absl::optional<OnMatch<DataType>> doMatch(const std::string& data) override {
    const auto itr = children_.find(data);
    if (itr != children_.end()) {
      return itr->second;
    }

    return absl::nullopt;
  }

private:
  absl::flat_hash_map<std::string, OnMatch<DataType>> children_;
};
} // namespace Matcher
} // namespace Envoy
