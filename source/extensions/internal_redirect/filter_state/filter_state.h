#pragma once

#include <string>

#include "envoy/router/internal_redirect.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

// Predicate that follows a redirect based on a string comparison against a
// filter-state object set earlier in the request. See filter_state_config.proto
// for semantics.
//
// Polarity is fixed at construction by which value was configured:
// * allow_value set  -> follow iff the object's value equals it (follow_on_match = true).
// * deny_value set   -> follow unless the object's value equals it (follow_on_match = false).
class FilterStatePredicate : public Router::InternalRedirectPredicate {
public:
  FilterStatePredicate(absl::string_view filter_state_key, absl::string_view compare_value,
                       bool follow_on_match, bool allow_if_absent)
      : filter_state_key_(filter_state_key), compare_value_(compare_value),
        follow_on_match_(follow_on_match), allow_if_absent_(allow_if_absent) {}

  bool acceptTargetRoute(StreamInfo::FilterState& filter_state, absl::string_view route_name,
                         bool downstream_is_https, bool target_is_https) override;

  absl::string_view name() const override {
    return "envoy.internal_redirect_predicates.filter_state";
  }

private:
  const std::string filter_state_key_;
  const std::string compare_value_;
  // Whether a value match means "follow the redirect": true for allow_value,
  // false for deny_value.
  const bool follow_on_match_;
  const bool allow_if_absent_;
};

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
