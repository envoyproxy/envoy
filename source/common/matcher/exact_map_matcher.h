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
  static absl::StatusOr<std::unique_ptr<ExactMapMatcher>>
  create(DataInputPtr<DataType>&& data_input, absl::optional<OnMatch<DataType>> on_no_match) {
    absl::Status creation_status = absl::OkStatus();
    auto ret = std::unique_ptr<ExactMapMatcher<DataType>>(
        new ExactMapMatcher<DataType>(std::move(data_input), on_no_match, creation_status));
    RETURN_IF_NOT_OK_REF(creation_status);
    return ret;
  }

  void addChild(std::string value, OnMatch<DataType>&& on_match) override {
    const auto itr_and_exists = children_.emplace(value, std::move(on_match));
    ASSERT(itr_and_exists.second);
  }

protected:
  template <class DataType2, class ActionFactoryContext> friend class MatchTreeFactory;

  ExactMapMatcher(DataInputPtr<DataType>&& data_input,
                  absl::optional<OnMatch<DataType>> on_no_match, absl::Status& creation_status)
      : MapMatcher<DataType>(std::move(data_input), std::move(on_no_match), creation_status) {}

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
