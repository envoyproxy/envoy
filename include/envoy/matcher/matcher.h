#pragma once

#include <memory>
#include <string>
#include <variant>

#include "envoy/config/common/matcher/v3/matcher.pb.h"

#include "absl/strings/string_view.h"
#include "external/envoy_api/envoy/config/core/v3/extension.pb.h"

namespace Envoy {

template <class DataType> class MatchTree;

template <class DataType> using MatchTreeSharedPtr = std::shared_ptr<MatchTree<DataType>>;

using TypedExtensionConfigOpt = absl::optional<envoy::config::core::v3::TypedExtensionConfig>;

// On match we either return a Any or we continue to match down a tree.
template <class DataType>
using OnMatch = std::pair<TypedExtensionConfigOpt, MatchTreeSharedPtr<DataType>>;

/**
 * MatchTree provides an extensible interface for matching on some input data.
 */
template <class DataType> class MatchTree {
public:
  virtual ~MatchTree() = default;

  // This encodes a three states:
  // Not enough data to complete the match: {false, {}}
  // Completed the match, no match: {true, {}}
  // Completed the match, match: {true, on_match}
  using MatchResult = std::pair<bool, absl::optional<OnMatch<DataType>>>;

  // Attempts to match against the matching data (which should contain all the data requested via
  // matching requirements). If the match couldn't be completed, {false, {}} will be returned.
  // If a match result was determined, {true, action} will be returned. If a match result was
  // determined to be no match, {true, {}} will be returned.
  virtual MatchResult match(const DataType& data) PURE;
};

} // namespace Envoy