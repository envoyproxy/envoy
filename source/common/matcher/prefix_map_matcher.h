#pragma once

#include "source/common/common/trie_lookup_table.h"
#include "source/common/matcher/map_matcher.h"

namespace Envoy {
namespace Matcher {

/**
 * Implementation of a trie match tree which resolves to the OnMatch with the longest matching
 * prefix.
 */
template <class DataType> class PrefixMapMatcher : public MapMatcher<DataType> {
public:
  static absl::StatusOr<std::unique_ptr<PrefixMapMatcher>>
  create(DataInputPtr<DataType>&& data_input, absl::optional<OnMatch<DataType>> on_no_match) {
    absl::Status creation_status = absl::OkStatus();
    auto ret = std::unique_ptr<PrefixMapMatcher<DataType>>(
        new PrefixMapMatcher<DataType>(std::move(data_input), on_no_match, creation_status));
    RETURN_IF_NOT_OK_REF(creation_status);
    return ret;
  }

  void addChild(std::string value, OnMatch<DataType>&& on_match) override {
    children_.add(value, std::make_shared<OnMatch<DataType>>(std::move(on_match)));
  }

protected:
  PrefixMapMatcher(DataInputPtr<DataType>&& data_input,
                   absl::optional<OnMatch<DataType>> on_no_match, absl::Status& creation_status)
      : MapMatcher<DataType>(std::move(data_input), std::move(on_no_match), creation_status) {}

  absl::optional<OnMatch<DataType>> doMatch(const std::string& data) override {
    const auto result = children_.findLongestPrefix(data);
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
