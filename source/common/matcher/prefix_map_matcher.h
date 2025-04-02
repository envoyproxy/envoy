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
        new PrefixMapMatcher<DataType>(std::move(data_input), on_no_match, creation_status, false));
    RETURN_IF_NOT_OK_REF(creation_status);
    return ret;
  }

  static absl::StatusOr<std::unique_ptr<PrefixMapMatcher>>
  createWithRetry(DataInputPtr<DataType>&& data_input,
                  absl::optional<OnMatch<DataType>> on_no_match) {
    absl::Status creation_status = absl::OkStatus();
    auto ret = std::unique_ptr<PrefixMapMatcher<DataType>>(
        new PrefixMapMatcher<DataType>(std::move(data_input), on_no_match, creation_status, true));
    RETURN_IF_NOT_OK_REF(creation_status);
    return ret;
  }

  void addChild(std::string value, OnMatch<DataType>&& on_match) override {
    children_.add(value, std::make_shared<OnMatch<DataType>>(std::move(on_match)));
  }

protected:
  PrefixMapMatcher(DataInputPtr<DataType>&& data_input,
                   absl::optional<OnMatch<DataType>> on_no_match, absl::Status& creation_status,
                   bool retry_shorter)
      : MapMatcher<DataType>(std::move(data_input), std::move(on_no_match), creation_status),
        retry_shorter_(retry_shorter) {}

  absl::optional<OnMatch<DataType>> doMatch(absl::string_view target,
                                            const DataType& data) override {
    while (true) {
      const std::shared_ptr<OnMatch<DataType>> result = children_.findLongestPrefix(target);
      if (result == nullptr) {
        return absl::nullopt;
      }
      if (result->action_cb_ || !retry_shorter_) {
        return *result;
      }
      ASSERT(result->matcher_);
      typename MatchTree<DataType>::MatchResult match = result->matcher_->match(data);
      if (match.match_state_ != MatchState::MatchComplete || match.on_match_.has_value()) {
        return match.on_match_;
      }
      // If a subtree lookup found neither match nor on_no_match, and retry_shorter_ is set,
      // we want to try again with a shorter matching prefix if one can be found.
      size_t prev_length = matchedPrefixLength(target);
      if (prev_length == 0) {
        return absl::nullopt;
      }
      target = target.substr(0, prev_length - 1);
    }
  }

  size_t matchedPrefixLength(absl::string_view str) {
    return children_.findLongestPrefixLength(str);
  }

private:
  TrieLookupTable<std::shared_ptr<OnMatch<DataType>>> children_;
  bool retry_shorter_;
};

} // namespace Matcher
} // namespace Envoy
