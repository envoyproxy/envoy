#pragma once

#include <memory>
#include <string>
#include <variant>

#include "envoy/config/common/matcher/v3/matcher.pb.h"

#include "absl/strings/string_view.h"
#include "external/envoy_api/envoy/config/core/v3/extension.pb.h"

namespace Envoy {

class MatchTree;
using MatchTreeSharedPtr = std::shared_ptr<MatchTree>;

using TypedExtensionConfigOpt = absl::optional<envoy::config::core::v3::TypedExtensionConfig>;

// On match we either return a Any or we continue to match down a tree.
using OnMatch = std::pair<TypedExtensionConfigOpt, MatchTreeSharedPtr>;

// Protocol specific matching data. For example, this can be used to present the buffered request
// data to the matcher.
class MatchingData {
public:
  virtual ~MatchingData() = default;
};
using MatchingDataSharedPtr = std::shared_ptr<MatchingData>;

/**
 * MatchTree provides an extensible interface for matching on some input data.
 */
class MatchTree {
public:
  virtual ~MatchTree() = default;

  // This encodes a three states:
  // Not enough data to complete the match: {false, {}}
  // Completed the match, no match: {true, {}}
  // Completed the match, match: {true, on_match}
  using MatchResult = std::pair<bool, absl::optional<OnMatch>>;

  // Attempts to match against the matching data (which should contain all the data requested via
  // matching requirements). If the match couldn't be completed, {false, {}} will be returned.
  // If a match result was determined, {true, action} will be returned. If a match result was
  // determined to be no match, {true, {}} will be returned.
  virtual MatchResult match(const MatchingData& data) PURE;
};

} // namespace Envoy