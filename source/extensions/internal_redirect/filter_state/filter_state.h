#pragma once

#include <string>

#include "envoy/router/internal_redirect.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

// Predicate that follows a redirect based on a boolean filter-state object
// set earlier in the request. See filter_state_config.proto for semantics.
//
// If the boolean value is true, the redirect is followed; if false, it is not.
// If the object is absent, redirect_if_absent_ determines the behavior.
class FilterStatePredicate : public Router::InternalRedirectPredicate {
public:
  FilterStatePredicate(absl::string_view redirect_enabled_key, bool redirect_if_absent)
      : redirect_enabled_key_(redirect_enabled_key), redirect_if_absent_(redirect_if_absent) {}

  bool acceptTargetRoute(StreamInfo::FilterState& filter_state, absl::string_view route_name,
                         bool downstream_is_https, bool target_is_https) override;

  absl::string_view name() const override {
    return "envoy.internal_redirect_predicates.filter_state";
  }

private:
  const std::string redirect_enabled_key_;
  const bool redirect_if_absent_;
};

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
