#pragma once

#include "source/common/common/utility.h"
#include "source/common/matcher/map_matcher.h"

namespace Envoy {
namespace Matcher {

/**
 * Implementation of a trie match tree which resolves to the OnMatch with the longest matching
 * prefix.
 */
template <class DataType> class PrefixMapMatcher : public MapMatcher<DataType> {
public:
  PrefixMapMatcher(DataInputPtr<DataType>&& data_input,
                   absl::optional<OnMatch<DataType>> on_no_match)
      : MapMatcher<DataType>(std::move(data_input), std::move(on_no_match)) {}

  void addChild(std::string value, OnMatch<DataType>&& on_match) override {
    children_.add(value, std::make_shared<OnMatch<DataType>>(std::move(on_match)));
  }

protected:
  absl::optional<OnMatch<DataType>> doMatch(const std::string& data) override {
    const auto result = children_.findLongestPrefix(data.c_str());
    if (result) {
      return *result;
    }

    return absl::nullopt;
  }

private:
  TrieLookupTable<std::shared_ptr<OnMatch<DataType>>> children_;
};

} // namespace Matcher
} // namespace Envoy
