#pragma once

#include <memory>
#include <string>
#include <variant>

#include "envoy/config/common/matcher/v3/matcher.pb.h"

#include "absl/strings/string_view.h"

namespace Envoy {

class MatchAction {
public:
  static MatchAction skip() { return MatchAction(absl::variant<bool, std::string>(true)); }

  static MatchAction callback(std::string callback) {
    return MatchAction(absl::variant<bool, std::string>(std::move(callback)));
  }

  static MatchAction fromProto(envoy::config::common::matcher::v3::MatchTree::MatchAction proto) {
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

  bool isSkip() { return std::get<bool>(action_); }

private:
  explicit MatchAction(absl::variant<bool, std::string> action) : action_(action) {}
  absl::variant<bool, std::string> action_;
};

// Protocol specific matching data. For example, this can be used to present the buffered request
// data to the matcher.
class MatchingData {
public:
  virtual ~MatchingData() = default;
};

class MatchTree {
public:
  virtual ~MatchTree() = default;

  // Attempts to match agains the matching data (which should contain all the data requested via
  // matching requirements). If no match is found, absl::nullopt will be returned.
  virtual absl::optional<MatchAction> match(const MatchingData& data) PURE;
};

using MatchTreeSharedPtr = std::shared_ptr<MatchTree>;

} // namespace Envoy