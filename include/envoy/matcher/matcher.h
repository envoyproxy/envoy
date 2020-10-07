#pragma once

#include <memory>
#include <string>
#include <variant>

#include "envoy/config/common/matcher/v3/matcher.pb.h"

#include "absl/strings/string_view.h"

namespace Envoy {

/**
 * MatchAction specifies the action to perform in response to matching a match tree.
 */
class MatchAction {
public:
  /**
   * Creates a Skip MatchAction.
   */
  static MatchAction skip() { return MatchAction(SkipOrCallback(Skip())); }

  /**
   * Creates a callback MatchAction that should invoke a match callback with the provided string.
   */
  static MatchAction callback(std::string callback) {
    return MatchAction(SkipOrCallback((std::move(callback))));
  }

  /**
   * Creates a MatchAction from the associated protobuf.
   */
  static MatchAction
  fromProto(const envoy::config::common::matcher::v3::MatchTree::MatchAction& proto) {
    if (proto.skip()) {
      return skip();
    } else {
      return callback(proto.callback());
    }
  }

  absl::optional<absl::string_view> callback() {
    if (absl::holds_alternative<std::string>(action_)) {
      return absl::string_view(std::get<std::string>(action_));
    }

    return absl::nullopt;
  }

  bool isSkip() { return absl::holds_alternative<Skip>(action_); }

private:
  struct Skip {};
  using SkipOrCallback = absl::variant<Skip, std::string>;

  explicit MatchAction(SkipOrCallback action) : action_(std::move(action)) {}

  const SkipOrCallback action_;
};

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

  using MatchResult = std::pair<bool, absl::optional<MatchAction>>;

  // Attempts to match against the matching data (which should contain all the data requested via
  // matching requirements). If the match couldn't be completed, {false, {}} will be returned.
  // If a match result was determined, {true, action} will be returned.
  virtual MatchResult match(const MatchingData& data) PURE;
};

using MatchTreeSharedPtr = std::shared_ptr<MatchTree>;

} // namespace Envoy