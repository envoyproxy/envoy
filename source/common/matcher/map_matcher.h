#pragma once

#include <string>

#include "envoy/matcher/matcher.h"

namespace Envoy {
namespace Matcher {

/**
 * Implementation of a map matcher which performs matches against the data provided by DataType.
 */
template <class DataType>
class MapMatcher : public MatchTree<DataType>, Logger::Loggable<Logger::Id::matcher> {
public:
  // Adds a child to the map.
  virtual void addChild(std::string value, OnMatch<DataType>&& on_match) PURE;

  MatchResult doNoMatch(const DataType& data, SkippedMatchCb skipped_match_cb) {
    if (data_input_->get(data).data_availability_ ==
        DataInputGetResult::DataAvailability::MoreDataMightBeAvailable) {
      return MatchResult::insufficientData();
    }
    return MatchTree<DataType>::handleRecursionAndSkips(on_no_match_, data, skipped_match_cb);
  }

  MatchResult match(const DataType& data, SkippedMatchCb skipped_match_cb = nullptr) override {
    const auto input = data_input_->get(data);
    ENVOY_LOG(trace, "Attempting to match {}", input);
    if (input.data_availability_ == DataInputGetResult::DataAvailability::NotAvailable) {
      return MatchResult::insufficientData();
    }

    // Returns `on_no_match` when input data is empty. (i.e., is absl::monostate).
    if (absl::holds_alternative<absl::monostate>(input.data_)) {
      return MatchTree<DataType>::handleRecursionAndSkips(on_no_match_, data, skipped_match_cb);
    }

    return doMatch(data, absl::get<std::string>(input.data_), skipped_match_cb);
  }

  template <class DataType2, class ActionFactoryContext> friend class MatchTreeFactory;
  MapMatcher(DataInputPtr<DataType>&& data_input, absl::optional<OnMatch<DataType>> on_no_match,
             absl::Status& creation_status)
      : data_input_(std::move(data_input)), on_no_match_(std::move(on_no_match)) {
    auto input_type = data_input_->dataInputType();
    if (input_type != DefaultMatchingDataType) {
      creation_status = absl::InvalidArgumentError(
          absl::StrCat("Unsupported data input type: ", input_type,
                       ", currently only string type is supported in map matcher"));
    }
  }

  const DataInputPtr<DataType> data_input_;
  const absl::optional<OnMatch<DataType>> on_no_match_;

  // The inner match method. Attempts to match against the resulting data string.
  // If a match is found, handleRecursionAndSkips must be called on it.
  // Otherwise MatchResult::noMatch() or MatchResult::insufficientData() should be returned.
  virtual MatchResult doMatch(const DataType& data, absl::string_view key,
                              SkippedMatchCb skipped_match_cb) PURE;
};

} // namespace Matcher
} // namespace Envoy
